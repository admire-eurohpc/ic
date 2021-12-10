#include <errno.h>
#include <margo.h>
#include <stdlib.h>             /* malloc */
#include <string.h>
#include <uuid.h>

#include "icc_rpc.h"


/* TODO
 * factorize with goto?
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
*/


/* RPC callbacks */
static void test_cb(hg_handle_t h, margo_instance_id mid);

struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;
  uint16_t          provider_id;
  char              clid[UUID_STR_LEN]; /* client uuid */
};


static hg_id_t rpc_hg_ids[ICC_RPC_COUNT] = { 0 };
static icc_callback_t rpc_callbacks[ICC_RPC_COUNT] = { NULL };


/* public functions */
int
icc_init(enum icc_log_level log_level, int bidir, struct icc_context **icc_context)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return -errno;

  icc->mid = margo_init(HG_PROVIDER,
                        bidir ? MARGO_SERVER_MODE : MARGO_CLIENT_MODE, 0, -1);
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
    margo_error(icc->mid, "Error opening IC address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = ICC_FAILURE;
    goto error;
  }
  free(path);

  char addr_str[ADDR_MAX_SIZE];
  if (!fgets(addr_str, ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from IC address file: %s", strerror(errno));
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

  icc->provider_id = MARGO_PROVIDER_ID_DEFAULT;

  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse(uuid, icc->clid);

  /* register client RPCs (i.e cb is NULL)
     XX could be a for loop */
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_TEST, NULL);
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_MALLEABILITY_AVAIL, NULL);
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_JOBMON_SUBMIT, NULL);
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_JOBMON_EXIT, NULL);
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_ADHOC_NODES, NULL);
  /* ... prep registration of other RPCs here */

  if (bidir) {
    REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_TARGET_ADDR_SEND, NULL);
    /* note this overwrites the previous registration without callback */
    REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_TEST, test_cb);
  }

  rc = register_rpcs(icc->mid, rpc_callbacks, rpc_hg_ids);
  if (rc) {
    margo_error(icc->mid, "Could not register RPCs");
    rc = ICC_FAILURE;
    goto error;
  }

  /* initialize RPC target */
  if (bidir == 1) {
    char addr_str[ADDR_MAX_SIZE];
    hg_size_t addr_str_size = ADDR_MAX_SIZE;
    target_addr_in_t rpc_in;
    int rpc_rc;

    if (get_hg_addr(icc->mid, addr_str, &addr_str_size)) {
      margo_error(icc->mid, "Could not get Mercury self address");
      rc = ICC_FAILURE;
      goto error;
    }

    char *jobid = getenv("SLURM_JOBID");
    rpc_in.jobid = jobid ? atoi(jobid) : 0;
    rpc_in.type = "app";
    rpc_in.addr_str = addr_str;
    rpc_in.provid = MARGO_PROVIDER_ID_DEFAULT;
    rpc_in.clid = icc->clid;

    rc = rpc_send(icc->mid, icc->addr, icc->provider_id,
                  rpc_hg_ids[ICC_RPC_TARGET_ADDR_SEND], &rpc_in, &rpc_rc);

    if (rc || rpc_rc) {
      margo_error(icc->mid, "Could not send target address of the bidirectional client");
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
icc_fini(struct icc_context *icc)
{
  int rc = ICC_SUCCESS;

  if (!icc)
    return rc;

  if (margo_addr_free(icc->mid, icc->addr) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not free Margo address");
    rc = ICC_FAILURE;
  }

  margo_finalize(icc->mid);
  free(icc);
  return rc;
}


int
icc_rpc_test(struct icc_context *icc, uint8_t number, int *retcode)
{
  int rc;
  test_in_t in;

  CHECK_ICC(icc);

  in.clid = icc->clid;
  in.number = number;

  rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[ICC_RPC_TEST], &in, retcode);

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

  rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[ICC_RPC_ADHOC_NODES], &in, retcode);

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

  rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[ICC_RPC_JOBMON_SUBMIT], &in, retcode);

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

  rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[ICC_RPC_JOBMON_EXIT], &in, retcode);

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

  rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[ICC_RPC_MALLEABILITY_AVAIL], &in, retcode);

  return rc ? ICC_FAILURE : ICC_SUCCESS;
}


/* RPC callbacks */
static void
test_cb(hg_handle_t h, margo_instance_id mid) {
  hg_return_t hret;
  test_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
  } else {
    margo_info(mid, "Got \"ICC\" RPC with argument %u\n", in.number);
  }

  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not respond to HPC");
  }
}
