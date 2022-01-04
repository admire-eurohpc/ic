#ifndef _ADMIRE_IC_CALLBACKS
#define _ADMIRE_IC_CALLBACKS

/**
 * Common IC server & client callbacks.
 */

void test_cb(hg_handle_t);
DEFINE_MARGO_RPC_HANDLER(test_cb);

#endif
