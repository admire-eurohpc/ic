#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>               /* log10, lround */
#include <margo.h>

#include "cbserver.h"
#include "rpc.h"
#include "icdb.h"
#include "icrm.h"                 /* ressource manager */

#define IOSETID_LEN 256
#define APPID_LEN 256


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

#define TIMESPEC_SET(t)                                                 \
  if (clock_gettime(CLOCK_MONOTONIC, &(t))) {                           \
    LOG_ERROR(mid, "clock_gettime: %s", strerror(errno));               \
  }

#define TIMESPEC_DIFF(end,start,r) {                    \
    (r).tv_sec = (end).tv_sec - (start).tv_sec;         \
    (r).tv_nsec = (end).tv_nsec - (start).tv_nsec;      \
    if ((r).tv_nsec < 0) {                              \
      (r).tv_sec--;                                     \
      (r).tv_nsec += 1000000000L;                       \
    }                                                   \
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
  ret = icdb_setclient(data->icdbs[xrank], in.clid, in.type, in.addr_str, in.nodelist ? in.nodelist : "", in.provid, in.jobid, in.jobncpus, in.jobnodelist ? in.jobnodelist : "", in.nprocs);
  if (ret != ICDB_SUCCESS) {
    if (data->icdbs[xrank]) {
      LOG_ERROR(mid, "Could not write client %s to database: %s", in.clid, icdb_errstr(data->icdbs[xrank]));
    }
    out.rc = RPC_FAILURE;
  }

  /* wake up the malleability thread */
  // ABT_mutex_lock(data->malldat->mutex);
  // data->malldat->sleep = 0;
  // data->malldat->jobid = in.jobid;
  // ABT_cond_signal(data->malldat->cond);
  // ABT_mutex_unlock(data->malldat->mutex);

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
  // ABT_mutex_lock(data->malldat->mutex);
  // data->malldat->sleep = 0;
  // data->malldat->jobid = jobid;
  // ABT_cond_signal(data->malldat->cond);
  // ABT_mutex_unlock(data->malldat->mutex);

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
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }
  margo_info(mid, "Slurm Job %"PRIu32".%"PRIu32" exited", in.jobid, in.jobstepid);

 respond:
  MARGO_RESPOND(h, out, hret);
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

  ret = icdb_reconfigurable(data->icdbs[xrank], in.clid, in.nprocs, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: icdb: %s", __func__, icdb_errstr(data->icdbs[xrank]));
    out.rc = RPC_FAILURE;
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
 * WITER seconds.
 */
static int
ioset_id(unsigned long witer, char *iosetid, size_t len) {
  unsigned int n;
  long round;

  /* XX TODO handle errors */
  /* double sec = witer/1000.0; */
  /* double a = log10(sec); */
  /* double b = lround(a); */

  round = lround(log10(witer));

  n = snprintf(iosetid, len, "%ld", round);
  if (n >= len) {            /* output truncated */
    return -1;
  }

  return 0;
}

static double
ioset_prio(unsigned long witer) {
  return pow(10, -lround(log10(witer)));
}

static double
ioset_scale(hm_t *iosets) {
  const char *setid;
  struct ioset *const *set;
  size_t curs = 0;
  double prio_min = INFINITY;

  /* find the lowest priority */
  while ((curs = hm_next(iosets, curs, &setid, (const void **)&set)) != 0) {
    if ((*set)->jobid != 0 && (*set)->priority < prio_min)
      prio_min = (*set)->priority;
  }

  if (prio_min == INFINITY) {
    return 0;
  }

  /* scale it to 1 */
  return 1 / prio_min;
}

static int
ioset_appid(unsigned long jobid, unsigned long jobstepid, char *appid, size_t len) {
  int n;
  n = snprintf(appid, len, "%lu.%lu", jobid, jobstepid);
  if (n < 0 || (unsigned)n >= len) {
    return -1;
  }
  return 0;
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
    if (!set) {
      LOG_ERROR(mid, "Out of memory");
      out.rc = RPC_FAILURE;
      ABT_rwlock_unlock(data->iosets_lock);
      goto respond;
    }
    set->priority = ioset_prio(in.ioset_witer);
    set->jobid = in.jobid;
    set->jobstepid = in.jobstepid;
    ABT_mutex_create(&set->lock);
    ABT_cond_create(&set->waitq);

    int rc;
    rc = hm_set(data->iosets, iosetid, &set, sizeof(set));
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

  /* get ioset time record */
  int rc;
  char appid[APPID_LEN];
  rc = ioset_appid(in.jobid, in.jobstepid, appid, APPID_LEN);
  if (rc) {
    LOG_ERROR(mid, "Could compute application ID from %lu.%lu", in.jobid, in.jobstepid);
    out.rc = RPC_FAILURE;
    goto respond;
  }

  ABT_rwlock_wrlock(data->ioset_time_lock);

  struct ioset_time *time;
  struct ioset_time *const *t = hm_get(data->ioset_time, appid);
  if (!t) {
    time = calloc(1, sizeof(struct ioset_time));
    if (!time) {
      LOG_ERROR(mid, "Out of memory");
      out.rc = RPC_FAILURE;
      ABT_rwlock_unlock(data->ioset_time_lock);
      goto respond;
    }

    int rc = hm_set(data->ioset_time, appid, &time, sizeof(time));
    if (rc == -1) {
      LOG_ERROR(mid, "Cannot set IO-set time data");
      out.rc = RPC_FAILURE;
      ABT_rwlock_unlock(data->ioset_time_lock);
      goto respond;
    }
  } else {
    time = *t;
  }

  ABT_rwlock_unlock(data->ioset_time_lock);

  /* record wait start time */
  if (in.iterflag) {
    TIMESPEC_SET(time->waitstart);
  }

  /* if there already is an application in the same set running, we go
   * to sleep on cond. When it finishes running, the condition will be
   * signaled to wake us up.
   */

  ABT_mutex_lock(set->lock);

  while (set->jobid != 0 &&
         (set->jobid != in.jobid || set->jobstepid != in.jobstepid)) {
    ABT_cond_wait(set->waitq, set->lock);
  }
  set->jobid = in.jobid;
  set->jobstepid = in.jobstepid;

  ABT_mutex_unlock(set->lock);

  /* we are the running app in the set, wait for our share */

  /* record wait end/io start time */
  if (in.iterflag) {
      TIMESPEC_SET(time->iostart);
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

  margo_debug(mid, "%"PRIu32".%"PRIu32" (set ID %s): %u IO slice%s",
              in.jobid, in.jobstepid, iosetid, out.nslices, out.nslices > 1 ? "s" : "");

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret);
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

  if (in.iterflag) {          /* reached end of IO phase */
    int rc;
    char appid[APPID_LEN];
    rc = ioset_appid(in.jobid, in.jobstepid, appid, APPID_LEN);
    if (rc) {
      LOG_ERROR(mid, "Could not compute application ID from %lu.%lu", in.jobid, in.jobstepid);
      out.rc = RPC_FAILURE;
      goto respond;
    }

    /* print out elapsed time */
    ABT_rwlock_rdlock(data->ioset_time_lock);
    struct ioset_time *const *t = hm_get(data->ioset_time, appid);
    ABT_rwlock_unlock(data->ioset_time_lock);
    if (!t) {
      LOG_ERROR(mid, "No IO-set timing data for app  %s", appid);
    }
    struct timespec ioend;
    TIMESPEC_SET(ioend);

    /* appid, witer, waitstart/cpuend, waitend/iostart, ioend, cpustart (next phase), nbytes */
    fprintf(data->ioset_outfile,
            "\"%s\",%"PRIu32",%lld.%.9ld,%lld.%.9ld,%lld.%.9ld,%"PRIu64"\n",
            appid, in.ioset_witer,
            (long long)(*t)->waitstart.tv_sec, (*t)->waitstart.tv_nsec,
            (long long)(*t)->iostart.tv_sec, (*t)->iostart.tv_nsec,
            (long long)ioend.tv_sec, ioend.tv_nsec,
            in.nbytes);

    if(fflush(data->ioset_outfile)) {
      LOG_ERROR(mid, "fflush IO-set result file: %s", strerror(errno));
    }

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
    if (set->jobid == in.jobid && set->jobstepid == in.jobstepid) {
      set->jobid = 0;
      set->jobstepid = 0;
      ABT_cond_signal(set->waitq);
    }
    ABT_mutex_unlock(set->lock);

    margo_debug(mid, "%"PRIu32".%"PRIu32" (set ID %s): IO phase end ",
                in.jobid, in.jobstepid, iosetid);
  }

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(hint_io_end_cb);


static void
lowmem_act(margo_instance_id mid, const struct cb_data *data) {
  int ret, xrank;
  ABT_GET_XRANK(ret, xrank);
  if (ret != ABT_SUCCESS) {
    return;
  }
  struct icdb_context *icdb = data->icdbs[xrank];
  struct icdb_client *c;

  size_t count;
  uint64_t cursor;

  do {
    c = NULL;
    /* XX filter on job ID */
    ret = icdb_getclients2(icdb, 0, "alert", &c, &count, &cursor);
    if (ret != ICDB_SUCCESS) {
      LOG_ERROR(mid, "lowmem: icdb getclients2: %s", icdb_errstr(icdb));
      break;
    }

    hg_addr_t addr;
    hg_return_t hret;
    int rpcret;
    for (size_t i = 0; i < count; i++) {
      hret = margo_addr_lookup(mid, c[i].addr, &addr);
      if (hret != HG_SUCCESS) {
        LOG_ERROR(mid, "lowmem: %s response hg address: %s", c[i].clid, HG_Error_to_string(hret));
        continue;
      }

      lowmem_in_t rin = { 0 };
      ret = rpc_send(mid, addr, data->rpcids[RPC_LOWMEM], &rin, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
      if (ret || rpcret) {
        LOG_ERROR(mid, "lowmem:  %s: RPC_LOWMEM failed", c[i].clid);
        continue;
      }
    }
    free(c);
  } while (cursor != 0);
}

void
metricalert_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid = NULL;
  metricalert_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);
  if (!data) {
    out.rc = RPC_FAILURE;
    LOG_ERROR(mid, "lowmem: no registered data");
    goto respond;
  }

  if (!strcmp(in.metric, "proxy_memory_used_percent")) {
    lowmem_act(mid, data);
  } else {
    margo_info(mid, "Got \""RPC_METRIC_ALERT_NAME"\" %s alert on %s for %s", in.active?"++ACTIVE++":"--INACTIVE--", in.source, in.pretty_print);
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(metricalert_cb);


void
alert_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret, xrank;
  alert_in_t in;
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

  struct icdb_context *icdb = data->icdbs[xrank];
  struct icdb_client c;

  ret = icdb_getlargestclient(icdb, &c);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "mall: icdb getlargest: %s", icdb_errstr(icdb));
    return;
  }

  char *newnodelist;
  ret = icdb_shrink(icdb, c.clid, &newnodelist);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "mall: icdb shrink: %s", icdb_errstr(icdb));
    return;
  }

  hg_addr_t addr;
  hg_return_t hret2;
  hret2 = margo_addr_lookup(mid, c.addr, &addr);
  if (hret2 != HG_SUCCESS) {
    LOG_ERROR(mid, "hg address: %s", HG_Error_to_string(hret2));
    goto respond;
  }

  int rpcret;
  reconfigure_in_t rin = { .cmdidx = 0, .maxprocs = 0, .hostlist = newnodelist };

  ret = rpc_send(mid, addr, data->rpcids[RPC_RECONFIGURE2], &rin, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
  if (ret) {
    LOG_ERROR(mid, "mall: client %s: RPC_RECONFIGURE2 send failed ", c.clid);
  } else if (rpcret) {
    LOG_ERROR(mid, "mall: client %s: RPC_RECONFIGURE2 returned %d", c.clid, rpcret);
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(alert_cb);

void
nodealert_cb(hg_handle_t h)
{
  hg_return_t hret;
  rpc_out_t out;
  out.rc = RPC_SUCCESS;

  margo_instance_id mid = margo_hg_handle_get_instance(h);
  if (!mid) {
    out.rc = RPC_FAILURE;
    goto respond;
  }

  /* XX here get rid of a the faulty node for given job */

respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(nodealert_cb);
