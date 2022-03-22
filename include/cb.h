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
 * Resource allocation request callback.
 *
 * RPC status code:
 * RPC_SUCCESS or RPC_FAILURE in case of error.
 */
void resalloc_cb(hg_handle_t);
DECLARE_MARGO_RPC_HANDLER(resalloc_cb);


struct cb_data {
  struct icrm_context *icrm;
  ABT_pool            icrm_pool;
};

#endif
