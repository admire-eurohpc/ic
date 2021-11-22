#include "icc_rpc.h"
#include "icc.h"


int
icc_hg_addr(margo_instance_id mid, char *addr_str, hg_size_t *addr_str_size)
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


/* Internal RPCs machinery */
#define ICC_REGISTER_RPC(mid,ids,cbs,idx,name,in,out) if (ids[idx]) {   \
    if (cbs[idx] == NULL) {                                     \
      ids[idx] = MARGO_REGISTER(mid, name, in, out, NULL);              \
    } else {                                                            \
      ids[idx] = MARGO_REGISTER_PROVIDER(mid, name, in, out, cb, ICC_MARGO_PROVIDER_ID_DEFAULT, ABT_POOL_NULL); \
      margo_register_data(mid, ids[idx], cbs[idx], NULL);               \
    }                                                                   \
  }


static void cb(hg_handle_t);
DEFINE_MARGO_RPC_HANDLER(cb)

static void
cb(hg_handle_t h)
{
  margo_instance_id mid = margo_hg_handle_get_instance(h);
  const struct hg_info* info = margo_get_info(h);

  icc_callback_t realcb = margo_registered_data(mid, info->id);
  realcb(h);
}
DECLARE_MARGO_RPC_HANDLER(cb)


int
register_rpcs(margo_instance_id mid, icc_callback_t callbacks[ICC_RPC_COUNT], hg_id_t ids[ICC_RPC_COUNT])
{
  /* Algorithm reminder:
     id == 0 => do not register RPC
     callback == NULL => register RPC as client
     callback != NULL => register RPC with given callback
  */

  if (!callbacks || ! ids) {
    margo_error(mid, "Invalid callbacks or Mercury ids table");
    return -1;
  }

  /* test RPC */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_TEST, "icc_test", test_in_t, rpc_out_t);

  /* job monitoring RPCs */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_JOBMON_SUBMIT, "icc_jobmon_submit", jobmon_submit_in_t, rpc_out_t);
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_JOBMON_SUBMIT, "icc_jobmon_exit", jobmon_exit_in_t, rpc_out_t);

  /* ad-hoc storage RPCs */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_JOBMON_SUBMIT, "icc_adhoc_nodes", jobmon_exit_in_t, rpc_out_t);

  return 0;
}
