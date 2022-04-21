#include <assert.h>             /* assert */
#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>              /* errno, strerror */
#include <inttypes.h>           /* uintXX */
#include <netdb.h>              /* addrinfo */
#include <stdlib.h>             /* malloc, getenv, setenv, strtoxx */
#include <string.h>             /* strerror */
#include <unistd.h>             /* close */
#include <margo.h>
#include "uuid_admire.h"

#include "hashmap.h"
#include "icc_priv.h"
#include "rpc.h"
#include "cb.h"
#include "cbcommon.h"
#include "flexmpi.h"


#define NBLOCKING_ES  64
#define CHECK_ICC(icc)  if (!(icc)) { return ICC_FAILURE; }


/* utils */

/**
 * Convert an ICC client type code to a string.
 */
static const char *_icc_type_str(enum icc_client_type type);

/**
 * Convert the string pointed by NPTR to an uint32_t and put the
 * result in the buffer pointed by DEST. NPTR and DEST must not be
 * NULL.
 *
 * DEST is set to 0 if no conversion takes place.
 *
 * Return 0 on success, -errno on error.
 */
static int _strtouint32(const char *nptr, uint32_t *dest);


static int _setup_margo(enum icc_log_level log_level, struct icc_context *icc);
static int _setup_reconfigure(struct icc_context *icc, icc_reconfigure_func_t func, void *data);
static int _setup_icrm(struct icc_context *icc);
static int _setup_hostmaps(struct icc_context *icc);
static int _register_client(struct icc_context *icc, int nprocs);

/* public functions */

int
icc_init(enum icc_log_level log_level, enum icc_client_type typeid, struct icc_context **icc) {
  return icc_init_mpi(log_level, typeid, 0, NULL, NULL, icc);
}

int
icc_init_mpi(enum icc_log_level log_level, enum icc_client_type typeid,
             unsigned int nprocs, icc_reconfigure_func_t func, void *data,
             struct icc_context **icc_context)
{
  int rc;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(*icc));
  if (!icc)
    return ICC_ENOMEM;

  /* note the use of calloc here, all icc members are initialized to
     zero: jobid, bidirectional, registered, etc. */

  icc->type = typeid;

  /*  apps that must be able to both receive AND send RPCs to the IC */
  if (typeid == ICC_TYPE_MPI || typeid == ICC_TYPE_FLEXMPI)
    icc->bidirectional = 1;

  /* resource manager jobid */
  char *jobid;
  jobid = getenv("SLURM_JOB_ID");
  if (!jobid) {
    jobid = getenv("SLURM_JOBID");
  }

  /* jobid is only required for registered clients */
  if (icc->bidirectional && !jobid) {
      margo_error(icc->mid, "icc (init): No JOB_ID found");
      rc = ICC_FAILURE;
      goto error;
  }

  if (jobid) {
    rc = _strtouint32(jobid, &icc->jobid);
    if (rc) {
      margo_error(icc->mid, "icc (init): Error converting job id \"%s\": %s", jobid, strerror(-rc));
      rc = ICC_FAILURE;
      goto error;
    }
  }

  /* client UUID, XX could be replaced with jobid.jobstepid? */
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse(uuid, icc->clid);

  rc = _setup_margo(log_level, icc);
  if (rc)
    goto error;

  /* icrm requires Argobots to be setup, so icrm goes after Margo */
  rc = _setup_icrm(icc);
  if (rc)
    goto error;

  rc = _setup_hostmaps(icc);
  if (rc)
    goto error;

  /* register reconfiguration func */
  if (func) {
    rc = _setup_reconfigure(icc, func, data);
    if (rc)
      goto error;
  }

  /* pass some data to callbacks that need it */
  margo_register_data(icc->mid, icc->rpcids[RPC_RECONFIGURE], icc, NULL);
  margo_register_data(icc->mid, icc->rpcids[RPC_RESALLOC], icc, NULL);

  /* register client last to avoid race conditions where the IC would
     send a RPC command before the client is fully set up */
  if (icc->bidirectional) {
    rc = _register_client(icc, nprocs);
    if (rc)
      goto error;
  }

  *icc_context = icc;

  return ICC_SUCCESS;

 error:
  icc_fini(icc);
  return rc;
}

int
icc_sleep(struct icc_context *icc, double timeout_ms)
{
  CHECK_ICC(icc);
  margo_thread_sleep(icc->mid, timeout_ms);
  return ICC_SUCCESS;
}


int
icc_wait_for_finalize(struct icc_context *icc)
{
  CHECK_ICC(icc);
  margo_wait_for_finalize(icc->mid);
  return ICC_SUCCESS;
}


int
icc_fini(struct icc_context *icc)
{
  int rc, rpcrc;

  rc = ICC_SUCCESS;

  if (!icc)
    return rc;

  if (icc->icrm_xstream) {
    icc->icrm_terminate = 1;
    ABT_xstream_free(&icc->icrm_xstream);
    /* pool is freed by ABT_xstream_free? */
    /* ABT_pool_free(&icc->icrm_pool); */
  }

  if (icc->bidirectional && icc->registered) {
    client_deregister_in_t in;
    in.clid = icc->clid;

    rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_DEREGISTER], &in, &rpcrc);
    if (rc || rpcrc) {
      margo_error(icc->mid, "Could not deregister target to IC");
    }
  }

  if (icc->hostlock) {
    ABT_rwlock_free(&icc->hostlock);
  }

  if (icc->hostalloc) {
    hm_free(icc->hostalloc);
  }

  if (icc->hostrelease) {
    hm_free(icc->hostrelease);
  }

  if (icc->icrm) {
    icrm_fini(&icc->icrm);
  }

  if (icc->flexhandle) {
    rc = dlclose(icc->flexhandle);
    if (rc) {
      margo_error(icc->mid, "%s", dlerror());
      rc = ICC_FAILURE;
    }
  }

  if (icc->flexmpi_sock != -1) {
    close(icc->flexmpi_sock);
  }

  if (icc->mid) {
    if (icc->addr) {
      margo_addr_free(icc->mid, icc->addr);
    }
    margo_finalize(icc->mid);
  }

  free(icc);

  return rc;
}


int
icc_rpc_test(struct icc_context *icc, uint8_t number, enum icc_client_type type, int *retcode)
{
  int rc;
  test_in_t in = { 0 };

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.number = number;
  in.type = _icc_type_str(type);

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_TEST], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_rpc_jobclean(struct icc_context *icc, uint32_t jobid, int *retcode)
{
  int rc;
  jobclean_in_t in;

  CHECK_ICC(icc);

  in.jobid = jobid;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBCLEAN], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


int
icc_rpc_adhoc_nodes(struct icc_context *icc, uint32_t jobid, uint32_t nnodes, uint32_t adhoc_nnodes, int *retcode)
{
  int rc;
  adhoc_nodes_in_t in;

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.jobid = jobid;
  in.nnodes = nnodes;
  in.adhoc_nnodes = adhoc_nnodes;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_ADHOC_NODES], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_rpc_jobmon_submit(struct icc_context *icc, uint32_t jobid, uint32_t jobstepid, uint32_t nnodes, int *retcode)
{
  int rc;
  jobmon_submit_in_t in;

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.jobid = jobid;
  in.jobstepid = jobstepid;
  in.nnodes = nnodes;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBMON_SUBMIT], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_rpc_jobmon_exit(struct icc_context *icc, uint32_t jobid, uint32_t jobstepid, int *retcode)
{
  int rc;
  jobmon_exit_in_t in;

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.jobid = jobid;
  in.jobstepid = jobstepid;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBMON_EXIT], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}



int
icc_rpc_malleability_avail(struct icc_context *icc, char *type, char *portname, uint32_t jobid, uint32_t nnodes, int *retcode)
{
  int rc;
  malleability_avail_in_t in;

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.type = type;
  in.portname = portname;
  in.jobid = jobid;
  in.nnodes = nnodes;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_MALLEABILITY_AVAIL], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


int
icc_rpc_malleability_region(struct icc_context *icc, enum icc_malleability_region_action type, int *retcode)
{
  int rc;
  malleability_region_in_t in;

  CHECK_ICC(icc);

  assert(type > ICC_MALLEABILITY_UNDEFINED && type <= UINT8_MAX);

  in.clid = icc->clid;
  in.type = type; /* safe to cast type to uint8 because of the check above */

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_MALLEABILITY_REGION], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


int
icc_release_resource(struct icc_context *icc, const char *host, uint16_t ncpus)
{
  CHECK_ICC(icc);

  if (!host || !ncpus)
    return ICC_EINVAL;

  int rc = ICC_SUCCESS;

  ABT_rwlock_wrlock(icc->hostlock);

  const uint16_t *nreleased = hm_get(icc->hostrelease, host);
  const uint16_t *nalloced = hm_get(icc->hostalloc, host);

  uint16_t n = ncpus + (nreleased ? *nreleased : 0);

  if (n < ncpus) {                              /* overflow */
    margo_error(icc->mid, "Too many CPUs to release");
    rc = ICC_FAILURE;
  }
  else if (n > (nalloced ? *nalloced : 0)) {  /* inconsistency */
    margo_error(icc->mid, "Too many CPUs released %s:%"PRIu16" (got %"PRIu16")",
                host, ncpus, (nalloced ? *nalloced : 0));
    rc = ICC_FAILURE;
  }
  else if (n == *nalloced) {                  /* ok to release */
    icrmerr_t ret = icrm_release_node(icc->icrm, host, icc->jobid, n);
    if (ret == ICC_SUCCESS) {
      margo_debug(icc->mid, "RELEASED %s:%"PRIu16, host, n);
      uint16_t nocpu = 0;
      rc = hm_set(icc->hostrelease, host, &nocpu, sizeof(nocpu));
      rc = hm_set(icc->hostalloc, host, &nocpu, sizeof(nocpu));
      if (rc == -1) rc = ICC_ENOMEM;
    }
    else {
      rc = ICC_FAILURE;
      margo_error(icc->mid, "Failure to release node %s", host);
      margo_debug(icc->mid, icrm_errstr(icc->icrm));
      rc = hm_set(icc->hostalloc, host, &n, sizeof(n));
      if (rc == -1) rc = ICC_ENOMEM;
    }
    /* if (n < nalloced) not all CPUs have been released yet */
  }
  ABT_rwlock_unlock(icc->hostlock);

  return rc;
}


static int
_setup_margo(enum icc_log_level log_level, struct icc_context *icc)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  assert(icc);

  /* use 2 extra threads: 1 for RPC network progress, 1 for background
     RPC callbacks */
  icc->mid = margo_init(HG_PROTOCOL,
                        icc->bidirectional ? MARGO_SERVER_MODE : MARGO_CLIENT_MODE, 1, 1);
  if (!icc->mid) {
    rc = ICC_FAILURE;
    goto end;
  }

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  char *path = icc_addr_file();
  if (!path) {
    margo_error(icc->mid, "Could not get ICC address file");
    rc = ICC_FAILURE;
    goto end;
  }

  FILE *f = fopen(path, "r");
  if (!f) {
    margo_error(icc->mid, "Could not open IC address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = ICC_FAILURE;
    goto end;
  }
  free(path);

  char addr_str[ICC_ADDR_LEN];
  if (!fgets(addr_str, ICC_ADDR_LEN, f)) {
    margo_error(icc->mid, "Could not read from IC address file: %s", strerror(errno));
    fclose(f);
    rc = ICC_FAILURE;
    goto end;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address from IC address file: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto end;
  }

  /* register RPCs. Note that if the callback is not NULL the client
     is able to send AND receive the RPC */
  for (int i = 0; i < RPC_COUNT; i++) {
    icc->rpcids[i] = 0;
  }

  icc->rpcids[RPC_TEST] = MARGO_REGISTER(icc->mid, RPC_TEST_NAME, test_in_t, rpc_out_t, test_cb);
  icc->rpcids[RPC_JOBMON_SUBMIT] = MARGO_REGISTER(icc->mid, RPC_JOBMON_SUBMIT_NAME, jobmon_submit_in_t, rpc_out_t, NULL);
  icc->rpcids[RPC_JOBMON_EXIT] = MARGO_REGISTER(icc->mid, RPC_JOBMON_EXIT_NAME, jobmon_exit_in_t, rpc_out_t, NULL);
  icc->rpcids[RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, RPC_ADHOC_NODES_NAME, adhoc_nodes_in_t, rpc_out_t, NULL);
  icc->rpcids[RPC_MALLEABILITY_AVAIL] = MARGO_REGISTER(icc->mid, RPC_MALLEABILITY_AVAIL_NAME, malleability_avail_in_t, rpc_out_t, NULL);

  if (icc->bidirectional) {
    icc->rpcids[RPC_CLIENT_REGISTER] = MARGO_REGISTER(icc->mid, RPC_CLIENT_REGISTER_NAME, client_register_in_t, rpc_out_t, NULL);
    icc->rpcids[RPC_CLIENT_DEREGISTER] = MARGO_REGISTER(icc->mid, RPC_CLIENT_DEREGISTER_NAME, client_deregister_in_t, rpc_out_t, NULL);
  }

  if (icc->type == ICC_TYPE_JOBCLEANER) {
    icc->rpcids[RPC_JOBCLEAN] = MARGO_REGISTER(icc->mid, RPC_JOBCLEAN_NAME, jobclean_in_t, rpc_out_t, NULL);
  }

  if (icc->type == ICC_TYPE_MPI || icc->type == ICC_TYPE_FLEXMPI) {
    icc->rpcids[RPC_RECONFIGURE] = MARGO_REGISTER(icc->mid, RPC_RECONFIGURE_NAME, reconfigure_in_t, rpc_out_t, reconfigure_cb);
    icc->rpcids[RPC_RESALLOC] = MARGO_REGISTER(icc->mid, RPC_RESALLOC_NAME, resalloc_in_t, rpc_out_t, resalloc_cb);
    icc->rpcids[RPC_RESALLOCDONE] = MARGO_REGISTER(icc->mid, RPC_RESALLOCDONE_NAME, resallocdone_in_t, rpc_out_t, NULL);
    icc->rpcids[RPC_MALLEABILITY_REGION] = MARGO_REGISTER(icc->mid, RPC_MALLEABILITY_REGION_NAME, malleability_region_in_t, rpc_out_t, NULL);
  }

 end:
  return rc;
}

static int
_setup_reconfigure(struct icc_context *icc, icc_reconfigure_func_t func, void *data)
{
  CHECK_ICC(icc);

  icc->flexhandle = NULL;

  if (icc->type == ICC_TYPE_FLEXMPI && !func) {
    /* XX TMP: dlsym FlexMPI reconfiguration function... */
    icc->flexmpi_func = flexmpi_func(icc->mid, &icc->flexhandle);
    if (!icc->flexmpi_func) {
      margo_info(icc->mid, "No FlexMPI reconfigure function, falling back to socket");
      /* ...or init socket to FlexMPI app */
      icc->flexmpi_sock = -1;
      icc->flexmpi_sock = flexmpi_socket(icc->mid, "localhost", "6666");
      if (icc->flexmpi_sock == -1) {
        margo_error(icc->mid, "%s: Could not initialize FlexMPI socket", __func__);
        return ICC_FAILURE;
      }
    }
  } else if (!func) {
    margo_error(icc->mid, "Invalid reconfigure function");
    return ICC_FAILURE;
  }

  icc->reconfig_func = func;
  icc->reconfig_data = data;

  return ICC_SUCCESS;
}

static int
_setup_icrm(struct icc_context *icc)
{
  int rc;

  icc->icrm_terminate = 0;

  /* setup a blocking pool to handle communication with the RM */
  rc = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE,
                             &icc->icrm_pool);
  if (rc != ABT_SUCCESS) {
    margo_debug(icc->mid, "ABT_pool_create_basic error: ret=%d", rc);
    return ICC_FAILURE;
  }

  rc = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &icc->icrm_pool,
                                ABT_SCHED_CONFIG_NULL, &icc->icrm_xstream);
  if (rc != ABT_SUCCESS) {
    margo_debug(icc->mid, "ABT_xstream_create_basic error: ret=%d", rc);
    return ICC_FAILURE;
  }

  rc = icrm_init(&icc->icrm);
  if (rc != ICRM_SUCCESS) {
    if (icc->icrm)
      margo_error(icc->mid, "icrm init: %s", icrm_errstr(icc->icrm));
    else
      margo_error(icc->mid, "icrm init failure (ret = %d)", rc);
    return ICC_FAILURE;
  }

  return ICC_SUCCESS;
}

static int
_setup_hostmaps(struct icc_context *icc)
{
  int rc = ABT_rwlock_create(&icc->hostlock);
  if (rc != ABT_SUCCESS)
    return ICC_FAILURE;

  icc->hostalloc = hm_create();
  if (!icc->hostalloc)
    return ICC_FAILURE;

  icc->hostrelease = hm_create();
  if (!icc->hostrelease)
    return ICC_FAILURE;

  return ICC_SUCCESS;
}

static int
_register_client(struct icc_context *icc, int nprocs)
{
  char addr_str[ICC_ADDR_LEN];
  hg_size_t addr_str_size = ICC_ADDR_LEN;
  client_register_in_t rpc_in;
  int rc;

  assert(icc);
  assert(icc->icrm);
  assert(icc->jobid);

  icc->provider_id = MARGO_PROVIDER_DEFAULT;

  if (get_hg_addr(icc->mid, addr_str, &addr_str_size)) {
    margo_error(icc->mid, "icc (register): Error getting self address");
    rc = ICC_FAILURE;
    goto end;
  }

  /* get job info from resource manager */
  icrm_ncpus(icc->icrm, icc->jobid, &rpc_in.jobncpus, &rpc_in.jobnnodes);

  rpc_in.nprocs = nprocs;
  rpc_in.addr_str = addr_str;
  rpc_in.jobid = icc->jobid;
  rpc_in.provid = icc->provider_id;
  rpc_in.clid = icc->clid;
  rpc_in.type = _icc_type_str(icc->type);

  int rpcret = RPC_SUCCESS;
  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_REGISTER], &rpc_in, &rpcret);

  if (rc || rpcret) {
    margo_error(icc->mid, "icc (register): Cannot register client to the IC (ret = %d, RPC ret = %d)", rc, rpcret);
    rc = ICC_FAILURE;
  } else {
    icc->registered = 1;
  }

 end:
  return rc;
}


static inline const char *
_icc_type_str(enum icc_client_type type)
{
  switch (type) {
  case ICC_TYPE_UNDEFINED:
    return "undefined";
  case ICC_TYPE_MPI:
    return "mpi";
  case ICC_TYPE_FLEXMPI:
    return "flexmpi";
  case ICC_TYPE_ADHOCCLI:
   return "adhoccli";
  case ICC_TYPE_JOBMON:
   return "jobmon";
  default:
    return "error";
  }
}

static int
_strtouint32(const char *nptr, uint32_t *dest)
{
  char *end;
  unsigned long long val;

  assert(nptr != NULL);
  assert(dest != NULL);

  *dest = 0;
  errno = 0;

  val = strtoull(nptr, &end, 0);

  if (errno != 0) {
    return -errno;
  }
  else if (end == nptr || (*end != '\0' && *end != '\n')) {
    return -EINVAL;
  }

  if (val > UINT32_MAX)
    return -EINVAL;

  *dest = val;

  return 0;
}
