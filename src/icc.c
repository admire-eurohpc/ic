#include <errno.h>
#include <margo.h>
#include <string.h>

#include "icc_rpc.h"


/* TODO
 * factorize with goto?
 * hg_error_to_string everywhere
 * Margo logging inside lib?
 * hello -> status
 * rpc_id array -> list of explicit rpc_id?
 * how to pass return struct to icc_client?
 * XX malloc intempestif,
 * strlen, strcpy?? => msg from server is safe?
 * Cleanup error/info messages
*/


struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;
  uint16_t          provider_id;
  hg_id_t           rpc_id[ICC_RPC_COUNT];
};


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

  icc->rpc_id[ICC_RPC_HELLO] = MARGO_REGISTER(icc->mid, "icc_hello", void, hello_out_t, NULL);
  icc->rpc_id[ICC_RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, "icc_adhoc_nodes", adhoc_nodes_in_t, adhoc_nodes_out_t, NULL);
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
icc_rpc_hello(struct icc_context *icc, int *retcode, char **retmsg)
{
  hg_return_t hret;
  hg_handle_t handle;

  hg_id_t rpc_id = icc->rpc_id[ICC_RPC_HELLO];

  hret = margo_create(icc->mid, icc->addr, rpc_id, &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  hret = margo_provider_forward_timed(icc->provider_id, handle, NULL, ICC_RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);      /* XX check error */
    return ICC_FAILURE;
  }

  hello_out_t resp;
  hret = margo_get_output(handle, &resp);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    *retcode = resp.rc;
    *retmsg = calloc(1, strlen(resp.msg) + 1);
    strcpy(*retmsg, resp.msg);

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


int
icc_rpc_adhoc_nodes(struct icc_context *icc,
                   uint32_t slurm_jobid,
                   uint32_t slurm_nnodes,
                   uint32_t adhoc_nnodes,
		   int *retcode)
{
  hg_return_t hret;
  hg_handle_t handle;

  hg_id_t rpc_id = icc->rpc_id[ICC_RPC_ADHOC_NODES];

  hret = margo_create(icc->mid, icc->addr, rpc_id, &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  adhoc_nodes_in_t in;
  in.slurm_jobid = slurm_jobid;
  in.slurm_nnodes = slurm_nnodes;
  in.adhoc_nnodes = adhoc_nnodes;

  hret = margo_provider_forward_timed(icc->provider_id, handle, &in, ICC_RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);	/* XX check error */
    return ICC_FAILURE;
  }

  adhoc_nodes_out_t resp;
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
