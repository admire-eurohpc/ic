#ifndef _ADMIRE_IC_CALLBACKS_COMMON
#define _ADMIRE_IC_CALLBACKS_COMMON

/**
 * Common IC clients & server callbacks. Server specific callbacks are
 * defined in cbserver.h (q.v.).
 */

void test_cb(hg_handle_t);
DECLARE_MARGO_RPC_HANDLER(test_cb);

#endif
