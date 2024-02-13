#ifndef _ADMIRE_IC_CB_SERVER
#define _ADMIRE_IC_CB_SERVER
/**
 * IC server callbacks. Some need access to the DB.
 */

#include "hashmap.h"
#include "uuid_admire.h"        /* UUID_STR_LEN */


void client_register_cb(hg_handle_t h);
void client_deregister_cb(hg_handle_t h);
void resallocdone_cb(hg_handle_t h);
void jobclean_cb(hg_handle_t h);
void jobmon_submit_cb(hg_handle_t h);
void jobmon_exit_cb(hg_handle_t h);
void adhoc_nodes_cb(hg_handle_t h);
void malleability_avail_cb(hg_handle_t h);
void malleability_region_cb(hg_handle_t h);
void hint_io_begin_cb(hg_handle_t h);
void hint_io_end_cb(hg_handle_t h);
void metricalert_cb(hg_handle_t h);
void alert_cb(hg_handle_t h);
void nodealert_cb(hg_handle_t h);

DECLARE_MARGO_RPC_HANDLER(client_register_cb);
DECLARE_MARGO_RPC_HANDLER(client_deregister_cb);
DECLARE_MARGO_RPC_HANDLER(resallocdone_cb);
DECLARE_MARGO_RPC_HANDLER(jobclean_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_submit_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_exit_cb);
DECLARE_MARGO_RPC_HANDLER(adhoc_nodes_cb);
DECLARE_MARGO_RPC_HANDLER(malleability_avail_cb);
DECLARE_MARGO_RPC_HANDLER(malleability_region_cb);
DECLARE_MARGO_RPC_HANDLER(hint_io_begin_cb);
DECLARE_MARGO_RPC_HANDLER(hint_io_end_cb);
DECLARE_MARGO_RPC_HANDLER(metricalert_cb);
DECLARE_MARGO_RPC_HANDLER(alert_cb);
DECLARE_MARGO_RPC_HANDLER(nodealert_cb);


/* XX fixme: duplication in structs */
struct malleability_data {
  ABT_mutex           mutex;    /* malleability thread mutex etc. */
  ABT_cond            cond;
  char                sleep;
  margo_instance_id   mid;
  char                clid[UUID_STR_LEN]; /* client triggering malleab. */
  uint32_t            jobid;              /* job triggering malleab. */
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
};

struct ioset {
  double    priority;
  long      jobid;              /* job of the running app, 0 if none */
  long      jobstepid;          /* jobstep of the running app */
  ABT_cond  waitq;              /* waiting queue for app in this set */
  ABT_mutex lock;
};

struct ioset_time {
  struct timespec iostart;      /* IO start time */
  struct timespec waitstart;    /* IO-set wait time */
};

struct cb_data {
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
  struct malleability_data *malldat;

  int       ioset_isrunning; /* an app is running, regardless of set */
  ABT_cond  iosetq;          /* queue for running apps, XX FIFO? */
  ABT_mutex iosetlock;

  hm_t      *iosets;         /* map of struct ioset, lock! */
  ABT_rwlock iosets_lock;

  hm_t      *ioset_time;     /*  map of elapsed IO/CPU time, lock! */
  ABT_rwlock ioset_time_lock;
  FILE      *ioset_outfile;  /*  ioset result file */
};

#endif
