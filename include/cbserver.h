#ifndef _ADMIRE_IC_CB_SERVER
#define _ADMIRE_IC_CB_SERVER

/**
 * IC server callbacks. Most need access to the DB.
 */

void client_register_cb(hg_handle_t h);
void client_deregister_cb(hg_handle_t h);
void jobclean_cb(hg_handle_t h);
void jobalter_cb(hg_handle_t h);
void jobmon_submit_cb(hg_handle_t h);
void jobmon_exit_cb(hg_handle_t h);
void adhoc_nodes_cb(hg_handle_t h);
void malleability_avail_cb(hg_handle_t h);
void malleability_region_cb(hg_handle_t h);

DECLARE_MARGO_RPC_HANDLER(client_register_cb);
DECLARE_MARGO_RPC_HANDLER(client_deregister_cb);
DECLARE_MARGO_RPC_HANDLER(jobclean_cb);
DECLARE_MARGO_RPC_HANDLER(jobalter_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_submit_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_exit_cb);
DECLARE_MARGO_RPC_HANDLER(adhoc_nodes_cb);
DECLARE_MARGO_RPC_HANDLER(malleability_avail_cb);
DECLARE_MARGO_RPC_HANDLER(malleability_region_cb);


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

struct cb_data {
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
  struct malleability_data *malldat;
};

#endif
