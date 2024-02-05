#include <assert.h>             /* assert */
#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>              /* errno, strerror */
#include <inttypes.h>           /* uintXX */
#include <netdb.h>              /* addrinfo */
#include <stdbool.h>			/* bool */
#include <stdio.h>
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

/**
 * Release HOST to the resource manager. The caller must get hostlock
 * before calling this function.
 */
static int release_node(struct icc_context *icc, const char *host);
static iccret_t clear_hostmap(hm_t *hostmap);

static int _setup_margo(enum icc_log_level log_level, struct icc_context *icc);
static int _setup_reconfigure(struct icc_context *icc, icc_reconfigure_func_t func, void *data);
static int _setup_icrm(struct icc_context *icc);
static int _setup_hostmaps(struct icc_context *icc);
static int _register_client(struct icc_context *icc, unsigned int nprocs);

/* public functions */

int
icc_init(enum icc_log_level log_level, enum icc_client_type typeid, struct icc_context **icc) {
  return icc_init_mpi(log_level, typeid, 0, NULL, NULL, icc);
}

int
icc_init_mpi(enum icc_log_level log_level, enum icc_client_type typeid,
             int nprocs, icc_reconfigure_func_t func, void *data,
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
  if (typeid == ICC_TYPE_MPI || typeid == ICC_TYPE_FLEXMPI ||
      typeid == ICC_TYPE_RECONFIG2 || typeid == ICC_TYPE_ALERT)
    icc->bidirectional = 1;

  /* resource manager stuff */
  char *jobid, *jobstepid, *nodelist;
  jobid = getenv("SLURM_JOB_ID");
  if (!jobid) {
    jobid = getenv("SLURM_JOBID");
  }
  jobstepid = getenv("SLURM_STEP_ID");
  if (!jobstepid) {
    jobstepid = getenv("SLURM_STEPID");
  }

  /* jobid is only required for registered clients */
  if (icc->bidirectional && !jobid) {
      margo_warning(MARGO_INSTANCE_NULL, "icc (init): job ID not found");
  }

  if (jobid) {
    rc = _strtouint32(jobid, &icc->jobid);
    if (rc) {
      margo_error(MARGO_INSTANCE_NULL, "icc (init): Error converting job id \"%s\": %s", jobid, strerror(-rc));
      rc = ICC_FAILURE;
      goto error;
    }
  }

  if (jobstepid) {
    rc = _strtouint32(jobstepid, &icc->jobstepid);
    if (rc) {
      margo_error(MARGO_INSTANCE_NULL, "icc (init): Error converting job step id \"%s\": %s", jobstepid, strerror(-rc));
      rc = ICC_FAILURE;
      goto error;
    }
  } else {
    icc->jobstepid = 0;
  }

  /* TODO TMP: if/when using, get the nodelist from the resource manager */
  nodelist = getenv("SLURM_JOB_NODELIST");
  if (nodelist) {
    icc->nodelist = strdup(nodelist);
    if (!icc->nodelist) {
      margo_error(icc->mid, "icc: init: nodelist failed: %s", strerror(errno));
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

  /* icrm requires Argobots to be setup, so goes after Margo */
  rc = _setup_icrm(icc);
  if (rc)
    goto error;

  rc = _setup_hostmaps(icc);
  if (rc)
    goto error;

  /* register reconfiguration func, automatic for FlexMPI */
  if (icc->type == ICC_TYPE_FLEXMPI || func) {
    rc = _setup_reconfigure(icc, func, data);
    if (rc)
      goto error;
  }

  /* pass some data to callbacks that need it */
  margo_register_data(icc->mid, icc->rpcids[RPC_RECONFIGURE], icc, NULL);
  margo_register_data(icc->mid, icc->rpcids[RPC_RECONFIGURE2], icc, NULL);
  margo_register_data(icc->mid, icc->rpcids[RPC_RESALLOC], icc, NULL);
  margo_register_data(icc->mid, icc->rpcids[RPC_LOWMEM], icc, NULL);

  /* nprocs should unsigned, but MPI defines it as an int, so we take
     care of the check here */
  if (nprocs < 0) {
    margo_error(icc->mid, "icc (init): Invalid number of processes");
    rc = ICC_FAILURE;
    goto error;
  }

  /* register client last to avoid race conditions where the IC would
     send a RPC command before the client is fully set up */
  if (icc->bidirectional) {
    rc = _register_client(icc, (unsigned)nprocs);
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

    rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_DEREGISTER], &in, &rpcrc, RPC_TIMEOUT_MS_DEFAULT);
    if (rc || rpcrc) {
      margo_error(icc->mid, "Could not deregister target to IC");
    }
  }

  if (icc->nodelist) {
    free(icc->nodelist);
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

  if (icc->reconfigalloc) {
    hm_free(icc->reconfigalloc);
  }

  icrm_fini();

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

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_TEST], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_rpc_jobclean(struct icc_context *icc, uint32_t jobid, int *retcode)
{
  int rc;
  jobclean_in_t in;

  CHECK_ICC(icc);

  in.jobid = jobid;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBCLEAN], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

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

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_ADHOC_NODES], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

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

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBMON_SUBMIT], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

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

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_JOBMON_EXIT], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

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

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_MALLEABILITY_AVAIL], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


int
icc_rpc_malleability_region(struct icc_context *icc, enum icc_malleability_region_action type, int procs_hint, int exclusive_hint, int *retcode)
{
  int rc;
  malleability_region_in_t in;

  CHECK_ICC(icc);

  if (type < ICC_MALLEABILITY_UNDEFINED || type > ICC_MALLEABILITY_LIM) {
    return ICC_FAILURE;
  }

  if (procs_hint < INT32_MIN || procs_hint > INT32_MAX) {
    return ICC_FAILURE;
  }

  in.clid = icc->clid;
  in.type = type;
  in.nprocs = procs_hint;
  in.nnodes = exclusive_hint ? procs_hint : 0;  /* one node per proc */

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_MALLEABILITY_REGION], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


iccret_t
icc_reconfig_pending(struct icc_context *icc, enum icc_reconfig_type *reconfigtype,
                     uint32_t *nprocs, const char **hostlist)
{
  *reconfigtype = ICC_RECONFIG_NONE;
  *nprocs = 0;
  *hostlist = NULL;

  ABT_rwlock_wrlock(icc->hostlock);

  if (icc->reconfig_flag == ICC_RECONFIG_NONE) {
    ABT_rwlock_unlock(icc->hostlock);
    return ICC_SUCCESS;
  }

  iccret_t rc = ICC_SUCCESS;

  *hostlist = icrm_hostlist(icc->reconfigalloc, 1, nprocs);
  if (!*hostlist) {
    if (*nprocs == UINT32_MAX) {
      rc = ICC_EOVERFLOW;
    } else {
      rc =  ICC_ENOMEM;
    }
  }

  /* XX fixme: make a copy of the nodelist? */
  if (*hostlist && !strcmp(*hostlist, "")) {
    *hostlist = icc->nodelist;
  }

  *reconfigtype = icc->reconfig_flag;

  /* reset reconfig data */
  icc->reconfig_flag = ICC_RECONFIG_NONE;
  rc = clear_hostmap(icc->reconfigalloc);

  ABT_rwlock_unlock(icc->hostlock);

  return rc;
}

iccret_t
icc_lowmem_pending(struct icc_context *icc, bool *lowmem)
{

  ABT_rwlock_wrlock(icc->lowmemlock);

  *lowmem = icc->lowmem;
  icc->lowmem = false;

  ABT_rwlock_unlock(icc->hostlock);

  return ICC_SUCCESS;
}

iccret_t
icc_hint_io_begin(struct icc_context *icc, unsigned long witer, int isfirst, unsigned int *nslices)
{
  assert(icc);

  if (!nslices) {
    return ICC_EINVAL;
  }

  if (witer > UINT32_MAX) {
    margo_error(icc->mid, "icc (hint_io_begin): IO-set characteristic time is too big");
    return ICC_FAILURE;
  }

  int rc = ICC_SUCCESS;

  hint_io_in_t in;
  in.jobid = icc->jobid;
  in.jobstepid = icc->jobstepid;
  in.ioset_witer = (uint32_t)witer;
  in.iterflag = isfirst ? 1 : 0;
  in.nbytes = 0;

  /* make RPC by hand instead of using rpc_send() because of the
     custom return struct */

  hg_return_t hret;
  hg_handle_t handle;
  hint_io_out_t resp;

  hret = margo_create(icc->mid, icc->addr, icc->rpcids[RPC_HINT_IO_BEGIN], &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "icc (hint_io_begin): RPC creation failure: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  /* we expect to block until it is our turn to run, so no timeout */
  hret = margo_forward(handle, &in);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "icc (hint_io_begin): RPC forwarding failure: %s", HG_Error_to_string(hret));
    if (hret != HG_NOENTRY) {
      hret = margo_destroy(handle);
      return ICC_FAILURE;
    }
  }

  hret = margo_get_output(handle, &resp);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "icc (hint_io_begin): Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    if (resp.rc != RPC_SUCCESS) {
      margo_error(icc->mid, "icc (hint_io_begin): RPC error: %d", resp.rc);
      rc = ICC_FAILURE;
    } else {
      *nslices = resp.nslices;
    }

    hret = margo_free_output(handle, &resp);
    if (hret != HG_SUCCESS) {
      margo_error(icc->mid, "icc (hint_io_begin): Could not free RPC output: %s", HG_Error_to_string(hret));
    }
  }

  hret = margo_destroy(handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "icc (hint_io_begin): Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  return rc;
}


iccret_t
icc_hint_io_end(struct icc_context *icc, unsigned long witer, int islast, unsigned long long nbytes)
{
  assert(icc);

  if (witer > UINT32_MAX) {
    margo_error(icc->mid, "icc (hint_io_end): IO-set characteristic time is too big");
    return ICC_FAILURE;
  }

  if (nbytes > UINT64_MAX) {
    margo_error(icc->mid, "icc (hint_io_end): nbytes too big to serialize");
    return ICC_FAILURE;
  }

  int rc = ICC_SUCCESS;

  int rpcret = RPC_SUCCESS;
  hint_io_in_t in;

  in.jobid = icc->jobid;
  in.jobstepid = icc->jobstepid;
  in.ioset_witer = (uint32_t)witer;
  in.iterflag = islast ? 1 : 0;
  in.nbytes = nbytes;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_HINT_IO_END], &in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
  if (rc || rpcret) {
    margo_error(icc->mid, "icc (hint_io_end): ret=%d, RPC ret= %d", rc, rpcret);
    rc = ICC_FAILURE;
  }

  return rc;
}


int icc_rpc_metric_alert(struct icc_context * icc, char * source, char * name, char * metric, char * operator, double current_value, int active, char * pretty_print, int *retcode)
{
  int rc = 0;
  metricalert_in_t in;

  CHECK_ICC(icc);

  in.source = source;
  in.name = name;
  in.metric = metric;
  in.operator = operator;
  char scurrent_val[32];
  (void)snprintf(scurrent_val, 32, "%g", current_value);
  in.current_value = scurrent_val;
  in.active = active;
  in.pretty_print = pretty_print;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_METRIC_ALERT], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);
  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


int
icc_rpc_alert(struct icc_context *icc, enum icc_alert_type type, int *retcode)
{
  int rc;
  alert_in_t in;

  CHECK_ICC(icc);

  assert(type > ICC_ALERT_UNDEFINED && type < ICC_ALERT_UNDEFINED && type <= UINT8_MAX);
  in.type = type;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_ALERT], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);
  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_rpc_nodealert(struct icc_context *icc, enum icc_alert_type type, const char *node, int *retcode)
{
  int rc;
  nodealert_in_t in;

  CHECK_ICC(icc);

  assert(type > ICC_ALERT_UNDEFINED && type < ICC_ALERT_UNDEFINED && type <= UINT8_MAX);
  in.type = type;
  in.nodename = node;
  in.jobid = icc->jobid;

  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_NODEALERT], &in, retcode, RPC_TIMEOUT_MS_DEFAULT);
  return rc ? ICC_FAILURE : ICC_SUCCESS;
}

int
icc_release_register(struct icc_context *icc, const char *host, uint16_t ncpus)
{
  CHECK_ICC(icc);

  if (!host || !ncpus)
    return ICC_EINVAL;

  if (ncpus <= 0) {
    return ICC_EINVAL;
  }

  int rc = ICC_SUCCESS;

  ABT_rwlock_wrlock(icc->hostlock);

  const uint16_t *nreleased = hm_get(icc->hostrelease, host);
  const uint16_t *nalloced = hm_get(icc->hostalloc, host);

  unsigned int n = (unsigned int)ncpus + (nreleased ? *nreleased : 0);

  if (n > UINT16_MAX || n < ncpus) {          /* overflow */
    margo_error(icc->mid, "Too many CPUs to release");
    rc = ICC_FAILURE;
  }
  else if (n > (nalloced ? *nalloced : 0)) {  /* inconsistency */
    /* catch all cases where nalloced = 0 since n is > 0  */
    margo_error(icc->mid, "Too many CPUs released %s:%"PRIu16" (got %"PRIu16")",
                host, ncpus, (nalloced ? *nalloced : 0));
    rc = ICC_FAILURE;
  }
  else {                                      /* register CPU(s) for release */
    rc = hm_set(icc->hostrelease, host, &n, sizeof(n));
    if (rc == -1) {
      rc = ICC_ENOMEM;
    }
  }

  ABT_rwlock_unlock(icc->hostlock);

  return rc;
}


int
icc_release_nodes(struct icc_context *icc)
{
  CHECK_ICC(icc);

  int rc;
  const char *host;
  size_t curs = 0;

  ABT_rwlock_wrlock(icc->hostlock);

  while ((curs = hm_next(icc->hostrelease, curs, &host, NULL)) != 0) {
    rc = release_node(icc, host);
    if (rc != ICC_SUCCESS) {
      break;
    }
  }

  ABT_rwlock_unlock(icc->hostlock);

  return rc;
}

static iccret_t
clear_hostmap(hm_t *hostmap)
{
  iccret_t rc = ICC_SUCCESS;
  const char *host = NULL;
  uint16_t nocpu = 0;
  size_t curs = 0;
  while ((curs = hm_next(hostmap, curs, &host, NULL)) != 0) {
    if (hm_set(hostmap, host, &nocpu, sizeof(nocpu)) == -1) {
      rc = ICC_ENOMEM;
      break;
    }
  }

  return rc;
}

static int
release_node(struct icc_context *icc, const char *host)
{
  CHECK_ICC(icc);
  if (!host) {
    return ICC_EINVAL;
  }

  const uint16_t *nreleased = hm_get(icc->hostrelease, host);
  if (!nreleased) {  /* ignore empty nodes */
    return ICC_SUCCESS;
  }

  char icrmerr[ICC_ERRSTR_LEN];
  int rc = icrm_release_node(host, icc->jobid, *nreleased, icrmerr);
  if (rc == ICRM_SUCCESS) {
    margo_debug(icc->mid, "Released %s:%"PRIu16, host, *nreleased);
    uint16_t nocpu = 0;
    if (hm_set(icc->hostrelease, host, &nocpu, sizeof(nocpu)) == -1) {
      return ICC_ENOMEM;
    }
    if (hm_set(icc->hostalloc, host, &nocpu, sizeof(nocpu)) == -1) {
      return ICC_ENOMEM;
    }
  } else {
    margo_info(icc->mid, "Not releasing node %s", host);
    margo_debug(icc->mid, icrmerr);
    if (rc == ICRM_EAGAIN) { /* not all CPUs released on node, ignore */
      rc = ICC_SUCCESS;
    } else {
      rc = ICC_FAILURE;
    }
  }

  return rc;
}


static int
_setup_margo(enum icc_log_level log_level, struct icc_context *icc)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  assert(icc);

  if (icc->bidirectional) {
    /* bidirectional: 2 extra ULTs: 1 for RPC network progress, 1 for
       background RPC callbacks */
    icc->mid = margo_init(HG_PROTOCOL, MARGO_SERVER_MODE, 1, 1);
  } else {
    /* client only: a single common ULT should be enough*/
    icc->mid = margo_init(HG_PROTOCOL, MARGO_CLIENT_MODE, 0, 0);
  }

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

  icc->rpcids[RPC_HINT_IO_BEGIN] = MARGO_REGISTER(icc->mid, RPC_HINT_IO_BEGIN_NAME, hint_io_in_t, hint_io_out_t, NULL);
  icc->rpcids[RPC_HINT_IO_END] = MARGO_REGISTER(icc->mid, RPC_HINT_IO_END_NAME, hint_io_in_t, rpc_out_t, NULL);

  if (icc->type == ICC_TYPE_ALERT) {
    icc->rpcids[RPC_LOWMEM] = MARGO_REGISTER(icc->mid, RPC_LOWMEM_NAME, lowmem_in_t, rpc_out_t, lowmem_cb);
  }

  icc->rpcids[RPC_METRIC_ALERT] = MARGO_REGISTER(icc->mid, RPC_METRIC_ALERT_NAME, metricalert_in_t, rpc_out_t, NULL);
  icc->rpcids[RPC_ALERT] = MARGO_REGISTER(icc->mid, RPC_ALERT_NAME, alert_in_t, rpc_out_t, NULL);
  icc->rpcids[RPC_NODEALERT] = MARGO_REGISTER(icc->mid, RPC_NODEALERT_NAME, nodealert_in_t, rpc_out_t, NULL);

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

  if (icc->type == ICC_TYPE_RECONFIG2) {
    icc->rpcids[RPC_RECONFIGURE2] = MARGO_REGISTER(icc->mid, RPC_RECONFIGURE2_NAME, reconfigure_in_t, rpc_out_t, reconfigure2_cb);
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
    icc->flexmpi_func = icc_flexmpi_func(icc->mid, &icc->flexhandle);
    if (!icc->flexmpi_func) {
      margo_info(icc->mid, "No FlexMPI reconfigure function, falling back to socket");
      /* ...or init socket to FlexMPI app */
      icc->flexmpi_sock = -1;
      icc->flexmpi_sock = icc_flexmpi_socket(icc->mid, "localhost", "6666");
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
  rc = ABT_pool_create_basic(ABT_POOL_FIFO_WAIT, ABT_POOL_ACCESS_MPMC, ABT_TRUE,
                             &icc->icrm_pool);
  if (rc != ABT_SUCCESS) {
    margo_debug(icc->mid, "ABT_pool_create_basic error: ret=%d", rc);
    return ICC_FAILURE;
  }

  rc = ABT_xstream_create_basic(ABT_SCHED_BASIC_WAIT, 1, &icc->icrm_pool,
                                ABT_SCHED_CONFIG_NULL, &icc->icrm_xstream);
  if (rc != ABT_SUCCESS) {
    margo_debug(icc->mid, "ABT_xstream_create_basic error: ret=%d", rc);
    return ICC_FAILURE;
  }

  icrm_init();

  return ICC_SUCCESS;
}

static int
_setup_hostmaps(struct icc_context *icc)
{
  int rc = ABT_rwlock_create(&icc->hostlock);
  if (rc != ABT_SUCCESS)
    return ICC_FAILURE;

  icc->reconfig_flag = ICC_RECONFIG_NONE;

  icc->hostalloc = hm_create();
  if (!icc->hostalloc)
    return ICC_FAILURE;

  icc->hostrelease = hm_create();
  if (!icc->hostrelease)
    return ICC_FAILURE;

  icc->reconfigalloc = hm_create();
  if (!icc->reconfigalloc)
    return ICC_FAILURE;

  return ICC_SUCCESS;
}

static int
_register_client(struct icc_context *icc, unsigned int nprocs)
{
  char addr_str[ICC_ADDR_LEN];
  hg_size_t addr_str_size = ICC_ADDR_LEN;
  client_register_in_t rpc_in;
  int rc;

  assert(icc);

  icc->provider_id = MARGO_PROVIDER_DEFAULT;

  if (get_hg_addr(icc->mid, addr_str, &addr_str_size)) {
    margo_error(icc->mid, "icc (register): Error getting self address");
    rc = ICC_FAILURE;
    goto end;
  }

  /* get job info from resource manager */
  char *jobnodelist = NULL;
  if (icc->jobid) {
    char icrmerr[ICC_ERRSTR_LEN];
    icrmerr_t icrmret;
    icrmret = icrm_info(icc->jobid, &rpc_in.jobncpus, &rpc_in.jobnnodes, &jobnodelist, icrmerr);
    if (icrmret != ICRM_SUCCESS) {
      margo_warning(icc->mid, "register_client: resource manager: %s", icrmerr);
    }
  }

  rpc_in.nprocs = nprocs;
  rpc_in.addr_str = addr_str;
  rpc_in.jobid = icc->jobid;
  rpc_in.provid = icc->provider_id;
  rpc_in.clid = icc->clid;
  rpc_in.nodelist = icc->nodelist;
  rpc_in.jobnodelist = jobnodelist;
  rpc_in.type = _icc_type_str(icc->type);

  int rpcret = RPC_SUCCESS;
  rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_REGISTER], &rpc_in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);

  if (rc || rpcret) {
    margo_error(icc->mid, "icc (register): Cannot register client to the IC (ret=%d, RPCret=%d)", rc, rpcret);
    rc = ICC_FAILURE;
  } else {
    icc->registered = 1;
  }

  free(jobnodelist);

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
  case ICC_TYPE_IOSETS:
   return "iosets";
  case ICC_TYPE_RECONFIG2:
   return "reconfig2";
  case ICC_TYPE_ALERT:
   return "alert";
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

  *dest = (uint32_t)val;

  return 0;
}
