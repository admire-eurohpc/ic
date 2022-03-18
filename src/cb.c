#include <assert.h>
#include <inttypes.h>
#include <margo.h>

#include "cb.h"
#include "rpc.h"                /* for RPC i/o structs */
#include "icrm.h"
#include "icc_priv.h"

#define MARGO_GET_INPUT(h,in,hret)  hret = margo_get_input(h, &in);	\
  if (hret != HG_SUCCESS) {						\
    margo_error(mid, "%s: Could not get RPC input", __func__);		\
  }

#define MARGO_RESPOND(h,out,hret)  hret = margo_respond(h, &out);	\
  if (hret != HG_SUCCESS) {					\
    margo_error(mid, "%s: Could not respond to RPC", __func__);	\
  }

#define MARGO_DESTROY_HANDLE(h,hret)  hret = margo_destroy(h);		\
  if (hret != HG_SUCCESS) {						\
    margo_error(mid, "%s Could not destroy Margo RPC handle: %s", __func__, HG_Error_to_string(hret)); \
  }


struct alloc_args {
  const struct icc_context *icc;
  char     shrink;
  uint32_t nnodes;
  int      retcode;
};

/**
 * Request a new allocation of ARGS.nnodes to the resource manager. If
 * ARGS.shrink is true, give back that many nodes (NOT IMPLEMENTED).
 *
 * The signature makes it suitable as an Argobots thread function
 * (with appropriate casts).
 *
 * Return ICC_SUCCESS or an error code in ARGS.retcode.
 */
static void _alloc_th(struct alloc_args *args);


void
resalloc_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  resalloc_in_t in;
  rpc_out_t out;
  int ret;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  const struct hg_info *info;
  const struct icc_context *icc;

  info = margo_get_info(h);
  icc = (struct icc_context *)margo_registered_data(mid, info->id);

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  /* dispatch allocation to an Argobots ULT that will block on the
     request. A tasklet would be more appropriate than an ULT here,
     but Argobots 1.x does not allow tasklets to call eventuals, which
     RPC do */
  struct alloc_args args = {
    .icc = icc,
    .shrink = in.shrink, .nnodes = in.nnodes,
    .retcode = ICC_SUCCESS
  };

  ret = ABT_thread_create(icc->icrm_pool, (void (*)(void *))_alloc_th, &args,
                          ABT_THREAD_ATTR_NULL, NULL);

  if (ret != ABT_SUCCESS) {
    margo_error(mid, "ABT_thread_create failure: ret=%d", ret);
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, hret)
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(resalloc_cb);


static void
_alloc_th(struct alloc_args *args)
{
  int ret, rpcret;
  resallocdone_in_t in;
  const struct icc_context *icc = args->icc;

  in.jobid = icc->jobid;
  in.shrink = args->shrink;
  in.nnodes = args->nnodes;
  in.hostlist = NULL;

  /* blocking call */
  ret = icrm_alloc(icc->icrm, in.jobid, args->shrink, &in.nnodes, &in.hostlist);

  if (ret != ICRM_SUCCESS) {
    args->retcode = ICC_FAILURE;
    margo_error(icc->mid, "icrm_alloc error: %s", icrm_errstr(icc->icrm));
    return;
  }

  ret = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_RESALLOCDONE],
                 &in, &rpcret);

  if (ret != ICC_SUCCESS)
    args->retcode = ICC_FAILURE;

  free(in.hostlist);
}
