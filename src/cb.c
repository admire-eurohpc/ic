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
  uint32_t ncpus;
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
static void alloc_th(struct alloc_args *args);



void
reconfigure_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  reconfigure_in_t in;
  rpc_out_t out;
  int rc;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Input failure RPC_RECONFIGURE: %s", HG_Error_to_string(hret));
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct icc_context *icc = (struct icc_context *)margo_registered_data(mid, info->id);

  if (!icc) {
    margo_error(mid, "RPC_RECONFIG: No reconfiguration data");
    out.rc = RPC_FAILURE;
    goto respond;
  }

  /* call registered function */
  if (icc->reconfig_func) {
    rc = icc->reconfig_func(0, in.maxprocs, in.hostlist, icc->reconfig_data);
  } else if (icc->type == ICC_TYPE_FLEXMPI ) {
    rc = flexmpi_reconfigure(mid, in.maxprocs, in.hostlist, icc->flexmpi_func, icc->flexmpi_sock);
  } else {
    rc = RPC_FAILURE;
  }
  out.rc = rc ? RPC_FAILURE : RPC_SUCCESS;

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Response failure RPC_RECONFIGURE: %s", HG_Error_to_string(hret));
  }
}
DEFINE_MARGO_RPC_HANDLER(reconfigure_cb);


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

  /* shrinking request can be dealt with immediately */
  if (in.shrink) {
    ret = icc->reconfig_func(in.shrink, in.ncpus, NULL, icc->reconfig_data);
    out.rc = ret ? RPC_FAILURE : RPC_SUCCESS;
    goto respond;
  }

  /* expand request: dispatch allocation to an Argobots ULT that will
     block on the request. A tasklet would be more appropriate than an
     ULT here, but Argobots 1.x does not allow tasklets to call
     eventuals, which RPC do */
  struct alloc_args *args = malloc(sizeof(*args));

  if (args == NULL) {
    out.rc = ICC_ENOMEM;
    goto respond;
  }

  args->icc = icc;
  args->ncpus = in.ncpus;
  args->retcode = ICC_SUCCESS;

  /* note: args must be freed in the ULT */

  ret = ABT_thread_create(icc->icrm_pool, (void (*)(void *))alloc_th, args,
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
alloc_th(struct alloc_args *args)
{
  const struct icc_context *icc = args->icc;

  resallocdone_in_t in = { 0 };
  in.ncpus = args->ncpus;
  in.jobid = icc->jobid;

  uint32_t newjobid;

  /* allocation request: blocking call */
  hm_t *newalloc;
  icrmerr_t ret = icrm_alloc(icc->icrm, icc->jobid, &newjobid, &in.ncpus,
                             &newalloc);
  if (ret != ICRM_SUCCESS) {
    margo_error(icc->mid, "icrm_alloc error: %s", icrm_errstr(icc->icrm));
    goto end;
  }

  in.hostlist = icrm_hostlist(newalloc, 1);

  margo_debug(icc->mid, "Job %"PRIu32" resource allocation of %"PRIu32" CPUs (%s)", in.jobid, in.ncpus, in.hostlist);

  /* CRITICAL SECTION: nodes can be deallocated at the same time +
     hostmap needs to be updated atomically */
  ABT_rwlock_wrlock(icc->hostlock);

  ret = icrm_merge(icc->icrm, newjobid);
  if (ret != ICRM_SUCCESS) {
    margo_error(icc->mid, "icrm_renounce error: %s", icrm_errstr(icc->icrm));
    goto end;
  }

  /* add new resources to hostalloc */
  ret = icrm_update_hostmap(icc->hostalloc, newalloc);
  if (ret == ICRM_EOVERFLOW) {
    margo_error(icc->mid, "Too many CPUs allocated");
    ABT_rwlock_unlock(icc->hostlock);
    goto end;
  }

  ABT_rwlock_unlock(icc->hostlock);

  /* inform the IC that the allocation succeeded */
  int rpcret = RPC_SUCCESS;
  ret = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_RESALLOCDONE], &in, &rpcret);
  if (ret != ICC_SUCCESS) {
    margo_error(icc->mid, "Error sending RPC_RESALLOCDONE");
    goto end;
  }

  if (rpcret == RPC_WAIT) {            /* do not do reconfigure */
    margo_debug(icc->mid, "Job %"PRIu32": not reconfiguring", in.jobid);
  } else if (rpcret == RPC_SUCCESS) {  /* reconfigure */
    margo_debug(icc->mid, "Job %"PRIu32": reconfiguring", in.jobid);
    ret = icc->reconfig_func(in.shrink, in.ncpus, in.hostlist, icc->reconfig_data);
  } else {
    margo_error(icc->mid, "Error in RPC_RESALLOCDONE");
  }

 end:
  free(args);
  if (newalloc)
    hm_free(newalloc);
  if (in.hostlist != NULL)
    free(in.hostlist);
}
