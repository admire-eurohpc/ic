#include <errno.h>
#include <margo.h>
#include <string.h>

#include "icc_rpc.h"


/* TODO
 * factorize with goto?
 * hg_error_to_string everywhere
 * Margo logging inside lib?
 * icc_status RPC (~dummy?)
 * Return struct or rc only?
 * XX malloc intempestif,
 * Cleanup error/info messages
 * margo_free_input in callback!
*/


struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;
  uint16_t          provider_id;
};


static hg_id_t rpc_hg_ids[ICC_RPC_COUNT];


int
icc_init(enum icc_log_level log_level, struct icc_context **icc_context)
{
  hg_return_t hret;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return ICC_FAILURE;

  icc->mid = margo_init(ICC_HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0);
  if (!icc->mid)
    goto error;

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  FILE *f = fopen(ICC_ADDR_FILE, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \""ICC_ADDR_FILE"\": %s", strerror(errno));
    goto error;
  }

  char addr_str[ICC_ADDR_MAX_SIZE];
  if (!fgets(addr_str, ICC_ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from Margo address file: %s", strerror(errno));
    fclose(f);
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address: %s", HG_Error_to_string(hret));
    goto error;
  }

  icc->provider_id = ICC_MARGO_PROVIDER_ID_DEFAULT;

  /* RPCs */
  rpc_hg_ids[ICC_RPC_TEST] = MARGO_REGISTER(icc->mid, "icc_test", test_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, "icc_adhoc_nodes", adhoc_nodes_in_t, rpc_out_t, NULL);
  /* register other RPCs here */

  *icc_context = icc;
  return ICC_SUCCESS;

 error:
  if (icc) {
    if (!icc->mid)
      margo_finalize(icc->mid);
    free(icc);
  }
  return ICC_FAILURE;
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
  hg_return_t hret;
  hg_handle_t handle;

  if (!icc)
    return ICC_FAILURE;

  if (!data) {
    margo_error(icc->mid, "Null RPC data argument");
    return ICC_FAILURE;
  }

  switch (rpc_code) {
  case ICC_RPC_TEST:
  case ICC_RPC_ADHOC_NODES:
    break;
  default:
    margo_error(icc->mid, "Unknown ICC RPC id %d", rpc_code);
    return ICC_FAILURE;
  }

  hret = margo_create(icc->mid, icc->addr, rpc_hg_ids[rpc_code], &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  /* XX cast public struct to HG struct, hackish and dangerous */
  hret = margo_provider_forward_timed(icc->provider_id, handle, data, ICC_RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);      /* XX check error */
    return ICC_FAILURE;
  }

  rpc_out_t resp;
  hret = margo_get_output(handle, &resp);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    *retcode = resp.rc;

    hret = margo_free_output(handle, &resp);
    if (hret != HG_SUCCESS) {
      margo_error(icc->mid, "Could not free RPC output: %s", HG_Error_to_string(hret));
    }
  }

  hret = margo_destroy(handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  return ICC_SUCCESS;
}
