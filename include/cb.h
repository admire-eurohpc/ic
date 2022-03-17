#ifndef _ADMIRE_IC_CALLBACKS_CLIENT
#define _ADMIRE_IC_CALLBACKS_CLIENT

/**
 * Clients callbacks.
 */

void resalter_cb(hg_handle_t);
DECLARE_MARGO_RPC_HANDLER(resalter_cb);

struct cb_data {
  struct icrm_context *icrm;
  ABT_pool            icrm_pool;
};

#endif
