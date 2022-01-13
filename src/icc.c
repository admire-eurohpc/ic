#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>
#include <netdb.h>              /* addrinfo */
#include <stdlib.h>             /* malloc */
#include <string.h>
#include <uuid.h>
#include <margo.h>

#include "rpc.h"
#include "cb.h"
#include "flexmpi.h"


#define CHECK_ICC(icc)  if (!(icc)) { return ICC_FAILURE; }


/* TODO
 * hg_error_to_string everywhere
 * Margo logging inside lib?
 * icc_status RPC (~test?)
 * Return struct or rc only?
 * XX malloc intempestif,
 * Cleanup error/info messages
 * margo_free_input in callback!
 * icc_init error code errno vs ICC? cleanup all
 * do we need an include directory?
 * Factorize boilerplate out of callbacks
 * Mercury macros move to .c?
 * ICC_TYPE_LEN check
 * RPC_CODE to string for logs (rpc.c)
 * Cleanup FlexMPI socket, get rid of global socket var
*/

struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;               /* server address */
  hg_id_t           rpcids[RPC_COUNT];  /* RPCs ids */
  uint8_t           bidirectional;
  uint16_t          provider_id;
  char              clid[UUID_STR_LEN]; /* client uuid */
  void              *flexhandle;        /* dlopen handle to FlexMPI library */
};


/* utils */

/**
 * Convert an ICC client type code to a string.
 */
static const char *_icc_type_str(enum icc_client_type type);


/* public functions */

int
icc_init(enum icc_log_level log_level, enum icc_client_type typeid, struct icc_context **icc_context)
{
  hg_return_t hret;
  int bidir;
  int rc = ICC_SUCCESS;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return -errno;

  bidir = 0;
  /*  FlexMPI apps must be able to both receive AND send RPCs to the IC */
  if (typeid == ICC_TYPE_FLEXMPI)
    bidir = 1;

  /* use 2 extra threads: 1 for RPC network progress, 1 for background
     RPC callbacks */
  icc->mid = margo_init(HG_PROTOCOL,
                        bidir ? MARGO_SERVER_MODE : MARGO_CLIENT_MODE, 1, 1);
  if (!icc->mid) {
    rc = ICC_FAILURE;
    goto error;
  }

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  char *path = icc_addr_file();
  if (!path) {
    margo_error(icc->mid, "Could not get ICC address file");
    rc = ICC_FAILURE;
    goto error;
  }

  FILE *f = fopen(path, "r");
  if (!f) {
    margo_error(icc->mid, "Could not open IC address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = ICC_FAILURE;
    goto error;
  }
  free(path);

  char addr_str[ICC_ADDR_LEN];
  if (!fgets(addr_str, ICC_ADDR_LEN, f)) {
    margo_error(icc->mid, "Could not read from IC address file: %s", strerror(errno));
    fclose(f);
    rc = ICC_FAILURE;
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address from IC address file: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto error;
  }

  /* set client UUID */
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse(uuid, icc->clid);

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

  if (bidir) {
    icc->rpcids[RPC_CLIENT_REGISTER] = MARGO_REGISTER(icc->mid, RPC_CLIENT_REGISTER_NAME, client_register_in_t, rpc_out_t, NULL);
    icc->rpcids[RPC_CLIENT_DEREGISTER] = MARGO_REGISTER(icc->mid, RPC_CLIENT_DEREGISTER_NAME, client_deregister_in_t, rpc_out_t, NULL);
  }

  if (typeid == ICC_TYPE_FLEXMPI) {
    icc->rpcids[RPC_FLEXMPI_MALLEABILITY] = MARGO_REGISTER(icc->mid, RPC_FLEXMPI_MALLEABILITY_NAME, flexmpi_malleability_in_t, rpc_out_t, flexmpi_malleability_cb);

    struct flexmpi_cbdata *d = malloc(sizeof(*d));

    /* get FlexMPI reconfiguration function */
    d->func = flexmpi_func(icc->mid, &icc->flexhandle);

    if (!d->func) {
      margo_info(icc->mid, "%s: No FlexMPI reconfiguration function, will fall back to socket", __func__);
    }

    /* init socket to FlexMPI app */
    d->sock = -1;
    d->sock = flexmpi_socket(icc->mid, "localhost", "6666");
    if (d->sock == -1) {
      margo_error(icc->mid, "%s: Could not initialize FlexMPI socket", __func__);
      rc = ICC_FAILURE;
      goto error;
    }
    /* pass data to callback */
    margo_register_data(icc->mid, icc->rpcids[RPC_FLEXMPI_MALLEABILITY], d, free);
  }

  /* send address to IC to be able to receive RPCs */
  if (bidir == 1) {
    char addr_str[ICC_ADDR_LEN];
    hg_size_t addr_str_size = ICC_ADDR_LEN;
    client_register_in_t rpc_in;
    int rpc_rc;

    icc->bidirectional = 1;
    icc->provider_id = MARGO_PROVIDER_DEFAULT;

    if (get_hg_addr(icc->mid, addr_str, &addr_str_size)) {
      margo_error(icc->mid, "Could not get Mercury self address");
      rc = ICC_FAILURE;
      goto error;
    }

    char *jobid = getenv("SLURM_JOBID");
    rpc_in.jobid = jobid ? atoi(jobid) : 0;
    rpc_in.addr_str = addr_str;
    rpc_in.provid = icc->provider_id;
    rpc_in.clid = icc->clid;
    rpc_in.type = _icc_type_str(typeid);

    rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_REGISTER], &rpc_in, &rpc_rc);

    if (rc || rpc_rc) {
      margo_error(icc->mid, "Could not register address of the bidirectional client to the IC");
      rc = ICC_FAILURE;
      goto error;
    }
  }

  *icc_context = icc;
  return ICC_SUCCESS;

 error:
  if (icc) {
    if (icc->mid)
      margo_finalize(icc->mid);
    free(icc);
  }
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
icc_fini(struct icc_context *icc)
{
  int rc, rpcrc;

  rc = ICC_SUCCESS;

  if (!icc)
    return rc;


  if (icc->bidirectional) {
    client_deregister_in_t in;
    in.clid = icc->clid;

    rc = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_CLIENT_DEREGISTER], &in, &rpcrc);

    if (rc || rpcrc) {
      margo_error(icc->mid, "Could not deregister target to IC");
    }

    if (margo_addr_free(icc->mid, icc->addr) != HG_SUCCESS) {
      margo_error(icc->mid, "Could not free Margo address");
      rc = ICC_FAILURE;
    }
  }

  if (icc->flexhandle) {
    rc = dlclose(icc->flexhandle);
    if (rc) {
      margo_error(icc->mid, "%s", dlerror());
      rc = ICC_FAILURE;
    }
  }

  margo_finalize(icc->mid);
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

