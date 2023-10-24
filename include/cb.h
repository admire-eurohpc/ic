#ifndef _ADMIRE_IC_CALLBACKS_CLIENT
#define _ADMIRE_IC_CALLBACKS_CLIENT

/**
 * Clients callbacks.
 */

/**
 * Reconfigure callback.
 *
 * RPC status code:
 * RPC_SUCCESS or RPC_FAILURE in case of error.
 */
void reconfigure_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(reconfigure_cb);

/**
 * Reconfigure2 callback (pull version).
 *
 * RPC status code:
 * RPC_SUCCESS or RPC_FAILURE in case of error.
 */
void reconfigure2_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(reconfigure2_cb);


/**
 * Resource allocation request callback.
 *
 * RPC status code:
 * RPC_SUCCESS or RPC_FAILURE in case of error.
 */
void resalloc_cb(hg_handle_t);
DECLARE_MARGO_RPC_HANDLER(resalloc_cb);

#endif
