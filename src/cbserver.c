#include <assert.h>
#include <inttypes.h>
#include <math.h>               /* log10, lround */
#include <margo.h>

#include "cbserver.h"
#include "rpc.h"
#include "icdb.h"
#include "icrm.h"                 /* ressource manager */

#define IOSETID_LEN 256

#define MARGO_GET_INPUT(h,in,hret)  hret = margo_get_input(h, &in);     \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not get RPC input");                          \
  }

#define MARGO_RESPOND(h,out,hret)  hret = margo_respond(h, &out);       \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not respond to RPC");                         \
  }

#define MARGO_DESTROY_HANDLE(h,hret)  hret = margo_destroy(h);          \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret)); \
  }

#define ABT_GET_XRANK(ret,xrank) ret = ABT_self_get_xstream_rank(&xrank); \
  if (ret != ABT_SUCCESS) {                                             \
    LOG_ERROR(mid, "Failed getting rank of ABT xstream ");              \
  }


void
client_register_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret, xrank;
  client_register_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  margo_info(mid, "Registering client %s", in.clid);

  const struct hg_info *info;
  const struct cb_data *data;

  info = margo_get_info(h);
  data = (struct cb_data *)margo_registered_data(mid, info->id);

  /* can happen if RPC is received before data is registered */
  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }

  assert(data->icdbs != NULL);

  /* write client to DB */
  ret = icdb_setclient(data->icdbs[xrank], in.clid, in.type, in.addr_str, in.provid, in.jobid, in.jobncpus, in.jobnnodes, in.nprocs);
  if (ret != ICDB_SUCCESS) {
    if (data->icdbs[xrank]) {
      LOG_ERROR(mid, "Could not write client %s to database: %s", in.clid, icdb_errstr(data->icdbs[xrank]));
    }
    out.rc = RPC_FAILURE;
  }

  /* wake up the malleability thread */
  ABT_mutex_lock(data->malldat->mutex);
  data->malldat->sleep = 0;
  data->malldat->jobid = in.jobid;
  ABT_cond_signal(data->malldat->cond);
  ABT_mutex_unlock(data->malldat->mutex);

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
  int ret, xrank;
  client_deregister_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  margo_info(mid, "Deregistering client %s", in.clid);

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }
  assert(data->icdbs != NULL);

  /* remove client from DB */
  uint32_t jobid;
  ret = icdb_delclient(data->icdbs[xrank], in.clid, &jobid);

  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not delete client %s: %s", __func__, in.clid, icdb_errstr(data->icdbs[xrank]));
    out.rc = RPC_FAILURE;
  }

  /* wake up the malleability thread */
  ABT_mutex_lock(data->malldat->mutex);
  data->malldat->sleep = 0;
  data->malldat->jobid = jobid;
  ABT_cond_signal(data->malldat->cond);
  ABT_mutex_unlock(data->malldat->mutex);

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(client_deregister_cb);


void
resallocdone_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  resallocdone_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  /* XX write to DB */
  margo_info(mid, "Job %"PRIu32": allocated %"PRIu32" CPUs (%s)",
             in.jobid, in.ncpus, in.hostlist);

 respond:
  MARGO_RESPOND(h, out, hret)
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(resallocdone_cb);


void
jobclean_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  jobclean_in_t in;
  rpc_out_t out;
  int ret, xrank;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  /* XX macro? */
  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }
  assert(data->icdbs != NULL);

  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  /* clean job from db, ask Slurm first */
  enum icrm_jobstate state;
  char icrmerrstr[ICC_ERRSTR_LEN];

  ret = icrm_jobstate(in.jobid, &state, icrmerrstr);
  if (ret != ICRM_SUCCESS) {
    LOG_ERROR(mid, "Ressource manager error: %s", icrmerrstr);
    /* cleanup DB even if the RM does not recognize the job */
    /* out.rc = RPC_FAILURE; */
    /* goto respond; */
  }

  if (state != ICRM_JOB_PENDING && state != ICRM_JOB_RUNNING) {
    margo_info(mid, "Job cleaner: Will cleanup job %"PRIu32, in.jobid);

    ret = icdb_deljob(data->icdbs[xrank], in.jobid);
    if (ret != ICDB_SUCCESS) {
      LOG_ERROR(mid, "Cleanup failure job %"PRIu32": %s", in.jobid, icdb_errstr(data->icdbs[xrank]));
      out.rc = RPC_FAILURE;
    }
  } else {
    margo_info(mid, "Job cleaner: ignoring running job %"PRIu32, in.jobid);
    out.rc = RPC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, hret)
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(jobclean_cb);


void
jobmon_submit_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret, xrank;
  jobmon_submit_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }
  assert(data->icdbs != NULL);

  margo_info(mid, "Job %"PRIu32".%"PRIu32" started on %"PRIu32" node%s",
             in.jobid, in.jobstepid, in.nnodes, in.nnodes > 1 ? "s" : "");

  ret = icdb_command(data->icdbs[xrank], "SET nnodes:%"PRIu32".%"PRIu32" %"PRIu32,
                     in.jobid, in.jobstepid, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(data->icdbs[xrank]));
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

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
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
  int ret, xrank;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }
  assert(data->icdbs != NULL);

  /* store nodes available for malleability in db */
  ret = icdb_command(data->icdbs[xrank], "HMSET malleability_avail:%"PRIu32" type %s portname %s nnodes %"PRIu32,
                     in.jobid, in.type, in.portname, in.nnodes);

  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(data->icdbs[xrank]));
    out.rc = RPC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(malleability_avail_cb);


void
malleability_region_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  malleability_region_in_t in;
  rpc_out_t out;
  int ret;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  margo_info(mid, "Application %s %s malleability region", in.clid,
             in.type == ICC_MALLEABILITY_REGION_ENTER ? "entering" : "leaving");

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(malleability_region_cb);


/**
 * Compute the IO-set ID corresponding to a characteristic time of
 * WITER_MS milliseconds.
 */
static int
ioset_id(unsigned long witer_ms, char *iosetid, size_t len) {
  unsigned int n;
  long round;

  /* XX TODO handle errors */
  /* double sec = witer_ms/1000.0; */
  /* double a = log10(sec); */
  /* double b = lround(a); */

  round = lround(log10(witer_ms/1000.0));

  n = snprintf(iosetid, len, "%ld", round);
  if (n >= len) {            /* output truncated */
    return -1;
  }

  return 0;
}

static double
ioset_prio(unsigned long witer_ms) {
  return pow(10, lround(log10(witer_ms/1000.0)));
}

static double
ioset_scale(hm_t *iosets) {
  const char *setid;
  struct ioset *const *set;
  size_t curs = 0;
  double prio_min = INFINITY;

  /* find the lowest priority */
  while ((curs = hm_next(iosets, curs, &setid, (const void **)&set)) != 0) {
    if ((*set)->isrunning && (*set)->priority < prio_min)
      prio_min = (*set)->priority;
  }

  if (prio_min == INFINITY) {
    return 0;
  }

  /* scale it to 1 */
  return 1 / prio_min;
}


void
hint_io_begin_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  hint_io_in_t in;
  hint_io_out_t out;
  int ret;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;
  out.nslices = 0;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }

  assert(data->iosetq != NULL);
  assert(data->iosetlock != NULL);
  assert(data->iosets != NULL);

  /* compute the IO-set ID */
  char iosetid[IOSETID_LEN];

  if (ioset_id(in.ioset_witer, iosetid, IOSETID_LEN)) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "Error computing IO-set from witer \"%"PRIu32"\"", in.ioset_witer);
    goto respond;
  }

  /* get a writer lock in case we need to initialize */
  ABT_rwlock_wrlock(data->iosets_lock);

  struct ioset *set;
  struct ioset *const *s = hm_get(data->iosets, iosetid);

  if (!s) {
    /* lazily initialize ioset data */
    set = calloc(1, sizeof(struct ioset));
    set->isrunning = 0;
    set->priority = ioset_prio(in.ioset_witer);
    ABT_mutex_create(&set->lock);
    ABT_cond_create(&set->waitq);

    int rc;
    rc = hm_set(data->iosets, iosetid, &set, sizeof(s));
    if (rc == -1) {
      LOG_ERROR(mid, "Cannot set IO-set data");
      out.rc = RPC_FAILURE;
      ABT_rwlock_unlock(data->iosets_lock);
      goto respond;
    }
  } else {
    set = *s;
  }

  ABT_rwlock_unlock(data->iosets_lock);

  if (in.phase_flag) {
    /* new phase : If an application in the set is running already we
     * go to sleep on cond. When it finishes running, the condition
     * will be signaled to wake us up.
     */
    ABT_mutex_lock(set->lock);
    while (set->isrunning) {
      ABT_cond_wait(set->waitq, set->lock);
    }
    set->isrunning = 1;
    ABT_mutex_unlock(set->lock);

    margo_debug(mid, "%"PRIu32".%"PRIu32" IO phase begin (setid %s)",
                in.jobid, in.jobstepid, iosetid);
  }

  ABT_mutex_lock(data->iosetlock);
  while (data->ioset_isrunning) {
    ABT_cond_wait(data->iosetq, data->iosetlock);
  }

  data->ioset_isrunning = 1;

  ABT_rwlock_rdlock(data->iosets_lock);
  double scale = ioset_scale(data->iosets);
  ABT_rwlock_unlock(data->iosets_lock);

  if (scale == 0) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "Wrong scale");
  }

  out.nslices = set->priority * scale;

  ABT_mutex_unlock(data->iosetlock);

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(hint_io_begin_cb);

void
hint_io_end_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  hint_io_in_t in;
  rpc_out_t out;
  int ret;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "No registered data");
    goto respond;
  }

  assert(data->iosets != NULL);
  assert(data->iosetq != NULL);
  assert(data->iosetlock != NULL);

  ABT_mutex_lock(data->iosetlock);
  data->ioset_isrunning = 0;
  ABT_cond_signal(data->iosetq);
  ABT_mutex_unlock(data->iosetlock);

  char iosetid[IOSETID_LEN];
  if (ioset_id(in.ioset_witer, iosetid, IOSETID_LEN)) {
      out.rc = RPC_FAILURE;
      LOG_ERROR(mid, "Could compute IO-set from witer \"%"PRIu32"\"", in.ioset_witer);
      goto respond;
  }

  if (in.phase_flag) {          /* reaching end of IO phase */

    ABT_rwlock_rdlock(data->iosets_lock);
    struct ioset *const *s = hm_get(data->iosets, iosetid);
    ABT_rwlock_unlock(data->iosets_lock);

    if (!s) {
      LOG_ERROR(mid, "No running IO-set with id %s", iosetid);
      goto respond;
    }

    struct ioset *set = *s;

    /* signal waiting app in the same set */
    ABT_mutex_lock(set->lock);
    set->isrunning = 0;
    ABT_cond_signal(set->waitq);
    ABT_mutex_unlock(set->lock);

    margo_debug(mid, "%"PRIu32".%"PRIu32" IO phase end (setid %s)",
                in.jobid, in.jobstepid, iosetid);
  }

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(hint_io_end_cb);
