#include <assert.h>
#include <inttypes.h>
#include <margo.h>

#include "rpc.h"
#include "icdb.h"


#define MARGO_GET_INPUT(h,in,hret)  hret = margo_get_input(h, &in);     \
  if (hret != HG_SUCCESS) {                                             \
    margo_error(mid, "%s: Could not get RPC input", __func__);          \
  }

#define MARGO_RESPOND(h,out,hret)  hret = margo_respond(h, &out);       \
  if (hret != HG_SUCCESS) {                                     \
    margo_error(mid, "%s: Could not respond to RPC", __func__); \
  }

#define MARGO_DESTROY_HANDLE(h,hret)  hret = margo_destroy(h);          \
  if (hret != HG_SUCCESS) {                                             \
    margo_error(mid, "%s Could not destroy Margo RPC handle: %s", __func__, HG_Error_to_string(hret)); \
  }


void
client_register_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  client_register_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  margo_info(mid, "Registering client %s", in.clid);

  const struct hg_info *info = margo_get_info(h);
  struct icdb_context **icdbs = (struct icdb_context **)margo_registered_data(mid, info->id);
  assert(icdbs != NULL);

  /* write client to DB */
  ret = icdb_setclient(icdbs[xrank], in.clid, in.type, in.addr_str, in.provid, in.jobid);
  if (ret != ICDB_SUCCESS) {
    if (icdbs[xrank]) {
      margo_error(mid, "%s: Could not write client %s to database: %s", __func__, in.clid, icdb_errstr(icdbs[xrank]));
    }
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(client_register_cb);


void
client_deregister_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  client_deregister_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  margo_info(mid, "Deregistering client %s", in.clid);

  const struct hg_info *info = margo_get_info(h);
  struct icdb_context **icdbs = (struct icdb_context **)margo_registered_data(mid, info->id);

  /* remove client from DB */
  ret = icdb_delclient(icdbs[xrank], in.clid);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not delete client %s: %s", __func__, in.clid, icdb_errstr(icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(client_deregister_cb);


void
jobmon_submit_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  jobmon_submit_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct icdb_context **icdbs = (struct icdb_context **)margo_registered_data(mid, info->id);

  margo_info(mid, "Job %"PRIu32".%"PRIu32" started on %"PRIu32" node%s",
             in.jobid, in.jobstepid, in.nnodes, in.nnodes > 1 ? "s" : "");

  ret = icdb_command(icdbs[xrank], "SET nnodes:%"PRIu32".%"PRIu32" %"PRIu32,
                     in.jobid, in.jobstepid, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(icdbs[xrank]));
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(jobmon_submit_cb);


void
jobmon_exit_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  jobmon_submit_in_t in;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  MARGO_GET_INPUT(h,in,hret);
  if (hret == HG_SUCCESS) {
    margo_info(mid, "Slurm Job %"PRIu32".%"PRIu32" exited", in.jobid, in.jobstepid);
  }

  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(jobmon_exit_cb);

void
adhoc_nodes_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  adhoc_nodes_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
  } else {
    margo_info(mid, "IC got adhoc_nodes request from job %"PRIu32": %"PRIu32" nodes (%"PRIu32" nodes assigned by Slurm)",
               in.jobid, in.nnodes, in.nnodes);
  }

  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(adhoc_nodes_cb);


void
malleability_avail_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  malleability_avail_in_t in;
  rpc_out_t out;
  int ret;
  int xrank;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct icdb_context **icdbs = (struct icdb_context **)margo_registered_data(mid, info->id);

  /* store nodes available for malleability in db */
  ret = icdb_command(icdbs[xrank], "HMSET malleability_avail:%"PRIu32" type %s portname %s nnodes %"PRIu32,
                     in.jobid, in.type, in.portname, in.nnodes);

  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(malleability_avail_cb);
