#ifndef _ADMIRE_IC_CB_SERVER
#define _ADMIRE_IC_CB_SERVER

/**
 * IC server callbacks. Most need access to the DB.
 */

void client_register_cb(hg_handle_t h);
void client_deregister_cb(hg_handle_t h);
void jobmon_submit_cb(hg_handle_t h);
void jobmon_exit_cb(hg_handle_t h);
void adhoc_nodes_cb(hg_handle_t h);
void malleability_avail_cb(hg_handle_t h);

DECLARE_MARGO_RPC_HANDLER(client_register_cb);
DECLARE_MARGO_RPC_HANDLER(client_deregister_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_submit_cb);
DECLARE_MARGO_RPC_HANDLER(jobmon_exit_cb);
DECLARE_MARGO_RPC_HANDLER(adhoc_nodes_cb);
DECLARE_MARGO_RPC_HANDLER(malleability_avail_cb);

#endif
