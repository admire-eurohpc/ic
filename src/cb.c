#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <margo.h>

#include "cb.h"
#include "rpc.h"                /* for RPC i/o structs */
#include "icrm.h"
#include "icdb.h"
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


/* shared variable to only allow one reconfig at a time */
static ABT_mutex_memory resalloc_mutex = ABT_MUTEX_INITIALIZER;
static int in_resalloc = 0;


struct alloc_args {
  struct icc_context *icc;
  uint32_t ncpus;
  uint32_t nnodes;
  int      retcode;
};

/**
 * Request a new allocation of ARGS.ncpus to the resource manager.
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
  if (!mid) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Error getting Margo instance");
    goto respond;
  }

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
    rc = icc_flexmpi_reconfigure(mid, 0, in.maxprocs, in.hostlist, icc->flexmpi_func, icc->flexmpi_sock);
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
  margo_info(mid, "resalloc_cb: begin"); // CHANGE JAVI

  out.rc = ICC_SUCCESS;

  const struct hg_info *info;
  struct icc_context *icc;

  info = margo_get_info(h);
  icc = (struct icc_context *)margo_registered_data(mid, info->id);

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  // CHANGE: JAVI
  // if shrinking kill pending jobs and wait until in_resalloc == 0
  if (in.shrink) {
    icrmerr_t ret = ICRM_SUCCESS;
    char errstr[ICC_ERRSTR_LEN];
    margo_info(mid, "resalloc_cb: begin icrm_kill_wait_pending_job");
    ret = icrm_kill_wait_pending_job(errstr);
    margo_info(mid, "resalloc_cb: end icrm_kill_wait_pending_job");
    if (ret != ICRM_SUCCESS) {
      margo_error(mid, "resalloc_cb: icrm_kill_wait_pending_job error: %s",errstr);
      goto respond;
    }
  }
  // END CHANGE: JAVI

  /* make sure no reconfiguration RPC is running */
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&resalloc_mutex);
  margo_info(mid, "resalloc_cb: lock"); // CHANGE JAVI
  ABT_mutex_lock(mutex);
    
  if (in_resalloc) {
    out.rc = RPC_EAGAIN;
    ABT_mutex_unlock(mutex);
    margo_info(mid, "resalloc_cb: unlock error"); // CHANGE JAVI

    goto respond;
  } else {
    in_resalloc = 1;
    ABT_mutex_unlock(mutex);
  }

  /* shrinking request can be dealt with immediately */
  if (in.shrink) {
    /* ALBERTO - Indicate if there is a restarting pending */
    if (icc->type == ICC_TYPE_STOPRESTART)
      icc->restarting = 1;

    if (icc->reconfig_func) {
      margo_info(mid, "resalloc_cb: call reconfig_func"); // CHANGE JAVI
      /* call callback immediately if available */
      ret = icc->reconfig_func(in.shrink, in.ncpus, NULL, icc->reconfig_data); /*ALBERTO - I need the hostlist, not NULL. Or new_nodes_list with the default values */
      margo_info(mid, "resalloc_cb: exit reconfig_func"); // CHANGE JAVI
      out.rc = ret ? RPC_FAILURE : RPC_SUCCESS;
    } else if (icc->type == ICC_TYPE_FLEXMPI) {
      ret = icc_flexmpi_reconfigure(icc->mid, in.shrink, in.ncpus, NULL, icc->flexmpi_func, icc->flexmpi_sock);
      out.rc = ret ? RPC_FAILURE : RPC_SUCCESS;
    } else {
      /* set flag to be polled later otherwise */
      ABT_rwlock_wrlock(icc->hostlock);
      icc->reconfig_flag = ICC_RECONFIG_SHRINK;
      ABT_rwlock_unlock(icc->hostlock);
    }
    ABT_mutex_lock(mutex);
    in_resalloc = 0;
    ABT_mutex_unlock(mutex);

    goto respond;
  }

  /* expand request: dispatch allocation to an Argobots ULT that will
     block on the request. A tasklet would be more appropriate than an
     ULT here, but Argobots 1.x does not allow tasklets to call
     eventuals, which RPC do */

  /* first, check that we are not being shut down */
  if (icc->icrm_terminate) {
    out.rc = RPC_TERMINATED;
    goto respond;
  }

  struct alloc_args *args = malloc(sizeof(*args));
  if (args == NULL) {
    out.rc = ICC_ENOMEM;
    goto respond;
  }

  args->icc = icc;
  args->ncpus = in.ncpus;
  args->nnodes = in.nnodes;
  args->retcode = ICC_SUCCESS;

  /* note: args must be freed in the ULT */

  if (icc->icrm_pool != ABT_POOL_NULL) {
    ret = ABT_thread_create(icc->icrm_pool, (void (*)(void *))alloc_th, args,
                            ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
      margo_error(mid, "ABT_thread_create failure: ret=%d", ret);
      out.rc = ICC_FAILURE;
    }
  }

 respond:
  margo_info(mid, "resalloc_cb: end"); // CHANGE JAVI
  MARGO_RESPOND(h, out, hret)
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(resalloc_cb);


static void
alloc_th(struct alloc_args *args)
{
  struct icc_context *icc = args->icc;
  ABT_mutex mutex;
    
  margo_info(icc->mid, "alloc_th: begin"); //CHANGE JAVI
    
  resallocdone_in_t in = { 0 };
  in.ncpus = args->ncpus;
  in.nnodes = args->nnodes;
  in.jobid = icc->jobid;
    
  uint32_t newjobid;
    
  /* allocation request: blocking call */
  hm_t *newalloc;
  char icrmerr[ICC_ERRSTR_LEN];
  // CHANGE JAVI
  //icrmerr_t icrmret = icrm_alloc(icc->jobid, &newjobid, &in.ncpus, &in.nnodes, &newalloc, icrmerr);
  icrmerr_t icrmret = icrm_alloc(&newjobid, &in.ncpus, &in.nnodes, &newalloc, icrmerr);
  if (icrmret == ICRM_ERESOURCEMAN) {
    margo_error(icc->mid, "alloc_th: Error allocating job: %s", icrmerr);
    goto end; //CHANGE: JAVI
    // END CHANGE JAVI
  } else if (icrmret != ICRM_SUCCESS) {
    margo_error(icc->mid, "icrm_alloc error: %s", icrmerr);
    goto error; //CHANGE: JAVI
  }

  in.hostlist = icrm_hostlist(newalloc, 1, NULL);
  if (!in.hostlist) {
    icrmret = ICRM_ENOMEM;
    margo_error(icc->mid, "icrm_hostlist error: out of memory");
    goto error; //CHANGE: JAVI
  }

  margo_debug(icc->mid, "Job %"PRIu32" resource allocation of %"PRIu32" cpus in %"PRIu32" nodes (%s)", in.jobid, in.ncpus, in.nnodes, in.hostlist);

  /* CRITICAL SECTION: nodes can be deallocated at the same time +
     hostmap needs to be updated atomically */
  ABT_rwlock_wrlock(icc->hostlock);

  // CHANGE JAVI
  //icrmret = icrm_merge(newjobid, icrmerr);
  //if (icrmret != ICRM_SUCCESS) {
  //  margo_error(icc->mid, "icrm_renounce error: %s", icrmerr);
  //  goto error;
  //}
  // END CHANGE JAVI

  /* add new resources to hostalloc */
  icrmret = icrm_update_hostmap(icc->hostalloc, newalloc);
  if (icrmret == ICRM_EOVERFLOW) {
    margo_error(icc->mid, "Too many CPUs allocated");
    ABT_rwlock_unlock(icc->hostlock);
    goto error; //CHANGE: JAVI
  }

  icrmret = icrm_update_hostmap(icc->reconfigalloc, newalloc);
  if (icrmret == ICRM_EOVERFLOW) {
    margo_error(icc->mid, "Too many CPUs allocated");
    ABT_rwlock_unlock(icc->hostlock);
    goto error; //CHANGE: JAVI
  }
  // CHANGE JAVI
  icrmret = icrm_update_hostmap_job(icc->hostjob, newalloc, newjobid);
  if (icrmret != ICDB_SUCCESS) {
    margo_error(icc->mid, "Host is already allocated on another jobid");
    ABT_rwlock_unlock(icc->hostlock);
    goto error; //CHANGE: JAVI
  }
    
  // add hostlist to redis database
  char *nodelist = icrm_hostlist(newalloc, 0, NULL);
  int icdbret = icdb_addnodes(icc->icdbs_cb, icc->clid, nodelist);
  free(nodelist);
  if (icdbret != ICDB_SUCCESS) {
      margo_error(icc->mid, "New hosts ca not be added to redis");
      ABT_rwlock_unlock(icc->hostlock);
      goto error; //CHANGE: JAVI
  }
  // END CHANGE JAVI

  ABT_rwlock_unlock(icc->hostlock);

  /* inform the IC that the allocation succeeded */
  int rpcret = RPC_SUCCESS;
  int ret = rpc_send(icc->mid, icc->addr, icc->rpcids[RPC_RESALLOCDONE],
                        &in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
  if (ret != ICC_SUCCESS) {
    margo_error(icc->mid, "Error sending RPC_RESALLOCDONE");
    goto error; //CHANGE: JAVI
  }

  if (rpcret == RPC_WAIT) {            /* do not do reconfigure */
    margo_debug(icc->mid, "Job %"PRIu32": not reconfiguring", in.jobid);
    goto error; //CHANGE: JAVI
  } else if (rpcret == RPC_SUCCESS) {  /* prepare reconfiguration */
    /* ALBERTO - Indicate if there is a restarting pending */
    if (icc->type == ICC_TYPE_STOPRESTART)
      icc->restarting = 1;

    if (icc->reconfig_func) {
      /* call callback immediately if available */
      margo_debug(icc->mid, "Job %"PRIu32": reconfiguring", in.jobid);
      ret = icc->reconfig_func(0, in.ncpus, in.hostlist, icc->reconfig_data);
    } else if (icc->type == ICC_TYPE_FLEXMPI) {
      ret = icc_flexmpi_reconfigure(icc->mid, 0, in.ncpus, in.hostlist, icc->flexmpi_func, icc->flexmpi_sock);
    } else {
      /* set flag to be polled later otherwise */
      ABT_rwlock_wrlock(icc->hostlock);
      icc->reconfig_flag = ICC_RECONFIG_EXPAND;
      ABT_rwlock_unlock(icc->hostlock);
    }
  } else {
    margo_error(icc->mid, "Error in RPC_RESALLOCDONE");
    goto error; //CHANGE: JAVI
  }


end:   //CHANGE: JAVI
  mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&resalloc_mutex);
margo_info(icc->mid, "alloc_th: before lock"); //CHANGE JAVI
  ABT_mutex_lock(mutex);
  in_resalloc = 0;
  ABT_mutex_unlock(mutex);
  margo_info(icc->mid, "alloc_th: after lock"); //CHANGE JAVI

error:   //CHANGE: JAVI
  margo_info(icc->mid, "alloc_th: icrm_clear_pending_job"); //CHANGE JAVI
  icrm_clear_pending_job(); //CHANGE: JAVI
  margo_info(icc->mid, "alloc_th: end"); //CHANGE JAVI
  free(args);
  if (newalloc)
    hm_free(newalloc);
  if (in.hostlist != NULL)
    free(in.hostlist);
}


void
reconfigure2_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  reconfigure_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  if (!mid) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Error getting Margo instance");
    goto respond;
  }

  out.rc = RPC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Input failure RPC_RECONFIGURE2: %s", HG_Error_to_string(hret));
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct icc_context *icc = (struct icc_context *)margo_registered_data(mid, info->id);

  if (!icc) {
    margo_error(mid, "RPC_RECONFIG2: no reconfiguration data");
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_rwlock_wrlock(icc->hostlock);

  /* set flag to be polled later */
  icc->reconfig_flag = in.shrink ? ICC_RECONFIG_SHRINK : ICC_RECONFIG_EXPAND;

  /* update nodelist */
  if (icc->nodelist) {
    free(icc->nodelist);
  }

  icc->nodelist = strdup(in.hostlist);
  if (!icc->nodelist) {
    margo_error(mid, "RPC_RECONFIGURE2 hostlist: %s", strerror(errno));
  	out.rc = RPC_FAILURE;
  }

  ABT_rwlock_unlock(icc->hostlock);

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Response failure RPC_RECONFIGURE2: %s", HG_Error_to_string(hret));
  }
}
DEFINE_MARGO_RPC_HANDLER(reconfigure2_cb);


void
lowmem_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  lowmem_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  if (!mid) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Error getting Margo instance");
    goto respond;
  }

  out.rc = RPC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "input failure RPC_LOWMEM: %s", HG_Error_to_string(hret));
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct icc_context *icc = (struct icc_context *)margo_registered_data(mid, info->id);
  if (!icc) {
    margo_error(mid, "RPC_LOWMEM: no registered data");
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_rwlock_wrlock(icc->lowmemlock);
  icc->lowmem = true;
  ABT_rwlock_unlock(icc->hostlock);

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Response failure RPC_LOWMEM: %s", HG_Error_to_string(hret));
  }
}
DEFINE_MARGO_RPC_HANDLER(lowmem_cb);
