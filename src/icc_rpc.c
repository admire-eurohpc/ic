#include "icc_rpc.h"
#include "icc.h"


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


/* Internal RPCs machinery */
#define ICC_REGISTER_RPC(mid,ids,cbs,idx,in,out)  if (ids[idx]) {       \
    if (cbs[idx] == NULL) {                                             \
      ids[idx] = MARGO_REGISTER(mid, "rpc_"#idx, in, out, NULL);        \
    } else {                                                            \
      ids[idx] = MARGO_REGISTER_PROVIDER(mid, "rpc_"#idx, in, out, cb, MARGO_PROVIDER_ID_DEFAULT, ABT_POOL_NULL); \
      struct rpc_data *d = calloc(1, sizeof(*d));                       \
      d->callback = cbs[idx];                                           \
      d->rpc_ids = ids;                                                 \
      margo_register_data(mid, ids[idx], d, free);                      \
    }                                                                   \
  }

struct rpc_data {
  hg_id_t             *rpc_ids;
  struct icdb_context *icdbs;
  icc_callback_t      callback;
};

static void cb(hg_handle_t);
DEFINE_MARGO_RPC_HANDLER(cb);

static void
cb(hg_handle_t h)
{
  /* XX factorize all callbacks here? */
  margo_instance_id mid = margo_hg_handle_get_instance(h);
  if (!mid)
    return;

  const struct hg_info *info = margo_get_info(h);
  const struct rpc_data *data = margo_registered_data(mid, info->id);

  /* real callback associated with the RPC */
  data->callback(h, mid);

  hg_return_t hret = margo_destroy(h);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
  }
}
DECLARE_MARGO_RPC_HANDLER(cb);

int
register_rpcs(margo_instance_id mid, icc_callback_t callbacks[ICC_RPC_COUNT], hg_id_t ids[ICC_RPC_COUNT])
{
  /* reminder:
     id == 0 => do not register RPC
     callback == NULL => register RPC as client
     callback != NULL => register RPC with given callback
  */

  if (!callbacks || ! ids) {
    margo_error(mid, "Invalid callbacks or Mercury ids table");
    return -1;
  }

  /* internal RPCs */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_TARGET_REGISTER, target_register_in_t, rpc_out_t);
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_TARGET_DEREGISTER, target_deregister_in_t, rpc_out_t);

  /* test RPC */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_TEST, test_in_t, rpc_out_t);

  /* availability for malleability RPC */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_MALLEABILITY_AVAIL, malleability_avail_in_t, rpc_out_t);

  /* job monitoring RPCs */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_JOBMON_SUBMIT, jobmon_submit_in_t, rpc_out_t);
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_JOBMON_EXIT, jobmon_exit_in_t, rpc_out_t);

  /* ad-hoc storage RPCs */
  ICC_REGISTER_RPC(mid, ids, callbacks, ICC_RPC_ADHOC_NODES, adhoc_nodes_in_t, rpc_out_t);

  return 0;
}


int
rpc_send(margo_instance_id mid, hg_addr_t addr, uint16_t provid, hg_id_t rpc_id,
         void *data, int *retcode)
{
  hg_return_t hret;
  hg_handle_t handle;

  hret = margo_create(mid, addr, rpc_id, &handle);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return -1;
  }

  hret = margo_provider_forward_timed(provid, handle, data, RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);      /* XX check error */
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
