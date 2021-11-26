#include <errno.h>
#include <margo.h>
#include <stdlib.h>             /* malloc */
#include <string.h>

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
    margo_error(icc->mid, "Error opening ICC address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = ICC_FAILURE;
    goto error;
  }
  free(path);

  char addr_str[ADDR_MAX_SIZE];
  if (!fgets(addr_str, ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from ICC address file: %s", strerror(errno));
    fclose(f);
    rc = ICC_FAILURE;
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address from ICC address file: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto error;
  }

  icc->provider_id = MARGO_PROVIDER_ID_DEFAULT;

  /* register client RPCs (i.e cb is NULL)
     XX could be a for loop */
  REGISTER_PREP(rpc_hg_ids, rpc_callbacks, ICC_RPC_TEST, NULL);
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

  rpc_hg_ids[ICC_RPC_MALLEABILITY_IN] = MARGO_REGISTER(icc->mid, "icc_malleabMan_in", malleabilityman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_OUT] = MARGO_REGISTER(icc->mid, "icc_malleabMan_out", malleabilityman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_IN] = MARGO_REGISTER(icc->mid, "icc_slurmMan_in", slurmman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_OUT] = MARGO_REGISTER(icc->mid, "icc_slurmMan_out", slurmman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_IOSCHED_OUT] = MARGO_REGISTER(icc->mid, "icc_iosched_out", iosched_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_OUT] = MARGO_REGISTER(icc->mid, "icc_adhocMan_out", adhocman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MONITOR_OUT] = MARGO_REGISTER(icc->mid, "icc_monitorMan_out", monitor_out_t, rpc_out_t, NULL);

  /* initialize RPC target */
  if (bidir) {
    char addr_str[ADDR_MAX_SIZE];
    hg_size_t addr_str_size = ADDR_MAX_SIZE;
    target_addr_in_t rpc_in;
    int rpc_rc;

    if (get_hg_addr(icc->mid, addr_str, &addr_str_size)) {
      margo_error(icc->mid, "Could not get Mercury self address");
      rc = ICC_FAILURE;
      goto error;
    }

    rpc_in.addr_str = addr_str;
    rpc_in.provid = MARGO_PROVIDER_ID_DEFAULT;
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
icc_init_opt(enum icc_log_level log_level, struct icc_context **icc_context, int server_id)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return -errno;

  icc->mid = margo_init(HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0);
  if (!icc->mid) {
    rc = ICC_FAILURE;
    goto error;
  }

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  char *path = icc_addr_file_opt(server_id);
  FILE *f = fopen(path, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = -errno;
    goto error;
  }
  free(path);

  char addr_str[ADDR_MAX_SIZE];
  if (!fgets(addr_str, ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from Margo address file: %s", strerror(errno));
    fclose(f);
    rc = -errno;
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto error;
  }

  icc->provider_id = MARGO_PROVIDER_ID_DEFAULT;

  /* RPCs */
  rpc_hg_ids[ICC_RPC_TEST] = MARGO_REGISTER(icc->mid, "icc_test", test_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_IN] = MARGO_REGISTER(icc->mid, "icc_malleabMan_in", malleabilityman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_OUT] = MARGO_REGISTER(icc->mid, "icc_malleabMan_out", malleabilityman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_IN] = MARGO_REGISTER(icc->mid, "icc_slurmMan_in", slurmman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_OUT] = MARGO_REGISTER(icc->mid, "icc_slurmMan_out", slurmman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_IOSCHED_OUT] = MARGO_REGISTER(icc->mid, "icc_iosched_out", iosched_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_OUT] = MARGO_REGISTER(icc->mid, "icc_adhocMan_out", adhocman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MONITOR_OUT] = MARGO_REGISTER(icc->mid, "icc_monitorMan_out", monitor_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_SUBMIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_submit", jobmon_submit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_EXIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_exit", jobmon_exit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, "icc_adhoc_nodes", adhoc_nodes_in_t, rpc_out_t, NULL);



  /* register other RPCs here */

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
icc_rpc_send(struct icc_context *icc, enum icc_rpc_code rpc_code, void *data, int *retcode) {
  if (!icc)
    return ICC_FAILURE;

  if (!data) {
    margo_error(icc->mid, "Null RPC data argument");
    return ICC_FAILURE;
  }

  if (rpc_code <= ICC_RPC_PRIVATE || rpc_code >= ICC_RPC_COUNT) {
    margo_error(icc->mid, "Unknown ICC RPC code %d", rpc_code);
    return ICC_FAILURE;
  }

  int rc = rpc_send(icc->mid, icc->addr, icc->provider_id, rpc_hg_ids[rpc_code],
                    data, retcode);

  if (rc)
    return ICC_FAILURE;
  return ICC_SUCCESS;
}


/* RPC callbacks */
static void
test_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  test_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
  } else {
    margo_info(mid, "Got \"test\" RPC with argument %u\n", in.number);
  }

  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not respond to HPC");
  }
}
