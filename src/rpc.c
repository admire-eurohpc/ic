#include <assert.h>

#include "icc.h"
#include "rpc.h"

#define ICC_ADDR_FILENAME  "icc.addr"
#define RPC_TIMEOUT_MS 2000


int
get_hg_addr(margo_instance_id mid, char *addr_str, hg_size_t *addr_str_size)
{
  hg_return_t hret;
  int rc = 0;

  if (!addr_str || *addr_str_size == 0) {
    margo_error(mid, "Wrong address string or size");
    return -1;
  }
  addr_str[0] = '\0';

  if(margo_is_listening(mid) == HG_FALSE) {
    margo_error(mid, "Margo instance is not a server");
    return -1;
  }

  hg_addr_t addr;

  hret = margo_addr_self(mid, &addr);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not get Margo self address: %s", HG_Error_to_string(hret));
    return -1;
  }

  hret = margo_addr_to_string(mid, addr_str, addr_str_size, addr);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not convert Margo self address to string: %s", HG_Error_to_string(hret));
    rc = -1;
  }

  hret = margo_addr_free(mid, addr);
  if (hret != HG_SUCCESS)
    margo_error(mid, "Could not free Margo address: %s", HG_Error_to_string(hret));

  return rc;
}


char *
icc_addr_file()
{
  const char *runtimedir = getenv("ADMIRE_DIR");
  if (!runtimedir)
    runtimedir = getenv("HOME");
  if (!runtimedir)
    runtimedir = ".";

  char *path = (char *)malloc(strlen(runtimedir) + strlen(ICC_ADDR_FILENAME) + 2);
  if (path) {
    sprintf(path, "%s/%s", runtimedir, ICC_ADDR_FILENAME);
  }
  return path;
}


int
rpc_send(margo_instance_id mid, hg_addr_t addr, hg_id_t rpcid,
         void *in, int *retcode)
{
  assert(addr);
  assert(rpcid);

  hg_return_t hret;
  hg_handle_t handle;

  hret = margo_create(mid, addr, rpcid, &handle);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return -1;
  }

  hret = margo_forward_timed(handle, in, RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));

    if (hret != HG_NOENTRY) {
      hret = margo_destroy(handle);
    }
    return -1;
  }

  rpc_out_t resp;
  hret = margo_get_output(handle, &resp);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    *retcode = resp.rc;

    hret = margo_free_output(handle, &resp);
    if (hret != HG_SUCCESS) {
      margo_error(mid, "Could not free RPC output: %s", HG_Error_to_string(hret));
    }
  }

  hret = margo_destroy(handle);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    return -1;
  }

  return 0;
}
