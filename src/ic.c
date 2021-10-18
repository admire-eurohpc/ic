#include <errno.h>
#include <margo.h>
#include <string.h>

#include "ic_rpc.h"


/* TODO
 * factorize with goto?
 * hg_error_to_string everywhere
 * Margo logging inside lib?
 * hello -> status
 * rpc_id array -> list of explicit rpc_id?
 * how to pass return struct to ic_client?
 * XX malloc intempestif,
 * strlen, strcpy?? => msg from server is safe?
*/


struct ic_context {
  margo_instance_id mid;
  hg_addr_t         addr;
  uint16_t          provider_id;
  hg_id_t           rpc_id[IC_RPC_COUNT];
};


int
ic_init(enum ic_log_level log_level, struct ic_context **ic_context)
{
  hg_return_t hret;

  *ic_context = NULL;

  struct ic_context *icc = calloc(1, sizeof(struct ic_context));
  if (!icc)
    return IC_FAILURE;

  icc->mid = margo_init(IC_HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0);
  if (!icc->mid)
    goto error;

  margo_set_log_level(icc->mid, ic_to_margo_log_level(log_level));

  FILE *f = fopen(IC_ADDR_FILE, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \""IC_ADDR_FILE"\": %s", strerror(errno));
    goto error;
  }

  char addr_str[IC_ADDR_MAX_SIZE];
  if (!fgets(addr_str, IC_ADDR_MAX_SIZE, f)) {
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

  icc->provider_id = IC_MARGO_PROVIDER_ID_DEFAULT;

  icc->rpc_id[IC_RPC_HELLO] = MARGO_REGISTER(icc->mid, "ic_hello", void, hello_out_t, NULL);
  /* register other RPCs here */

  *ic_context = icc;
  return IC_SUCCESS;

 error:
  if (icc) {
    if (!icc->mid)
      margo_finalize(icc->mid);
    free(icc);
  }
  return IC_FAILURE;
}


int
ic_fini(struct ic_context *icc)
{
  int rc = IC_SUCCESS;

  if (!icc)
    return rc;

  if (margo_addr_free(icc->mid, icc->addr) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not free Margo address");
    rc = IC_FAILURE;
  }

  margo_finalize(icc->mid);
  free(icc);
  return rc;
}


int
ic_rpc_hello(struct ic_context *icc, int *retcode, char **retmsg)
{
  hg_return_t hret;
  hg_handle_t handle;

  hg_id_t rpc_id = icc->rpc_id[IC_RPC_HELLO];

  hret = margo_create(icc->mid, icc->addr, rpc_id, &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return IC_FAILURE;
  }

  hret = margo_provider_forward_timed(icc->provider_id, handle, NULL, IC_RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);	/* XX check error */
    return IC_FAILURE;
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
    return IC_FAILURE;
  }

  return IC_SUCCESS;
}
