#include <assert.h>
#include <inttypes.h>
#include <margo.h>

#include "rpc.h"                /* for RPC i/o structs */


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


void
test_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  test_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);

  if (hret == HG_SUCCESS) {
    margo_info(mid, "Got \""RPC_TEST_NAME"\" RPC with argument %u\n", in.number);
  } else {
    out.rc = ICC_FAILURE;
  }
  MARGO_RESPOND(h, out, hret)
  MARGO_DESTROY_HANDLE(h, hret);
}
DECLARE_MARGO_RPC_HANDLER(test_cb);
