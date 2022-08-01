#ifndef _ADMIRE_IC_CB_SERVER
#define _ADMIRE_IC_CB_SERVER
/**
 * IC server callbacks. Some need access to the DB.
 */

#include "hashmap.h"


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


/* XX fixme: duplication in structs */
struct malleability_data {
  ABT_mutex           mutex;    /* malleability thread mutex etc. */
  ABT_cond            cond;
  char                sleep;
  margo_instance_id   mid;
  uint32_t            jobid;    /* job that triggered malleability */
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
};

struct ioset {
  long long setid;
  int       *isrunning;
  ABT_mutex mutex;
  ABT_cond  cond;
};

struct cb_data {
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
  struct malleability_data *malldat;

  hm_t       *iosets;         /* map of IO-set data, lock! */
  ABT_rwlock iosets_lock;
};

#endif
