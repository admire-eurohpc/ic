#ifndef ADMIRE_IC_H
#define ADMIRE_IC_H

struct ic_context;

#define IC_SUCCESS  0
#define IC_FAILURE -1


/* Log levels lifted from Margo */
enum ic_log_level {
    IC_LOG_EXTERNAL,
    IC_LOG_TRACE,
    IC_LOG_DEBUG,
    IC_LOG_INFO,
    IC_LOG_WARNING,
    IC_LOG_ERROR,
    IC_LOG_CRITICAL
};

enum ic_rpc {
  IC_RPC_ERROR,
  IC_RPC_HELLO,
  IC_RPC_COUNT
};

struct ic_rpc_ret {
  int  retcode;
  char *msg;
};


/**
 * Initialize a Margo client context.
 * Return IC_SUCCESS or error code.
*/
int ic_init(enum ic_log_level log_level, struct ic_context **icc);

/**
 * Finalize the Margo instance associated with ic_context ICC.
 */
int ic_fini(struct ic_context *icc);

/**
 * Make the "hello" RPC.
 *
 * Fill RETCODE and RETMSG with the the code and return message from
 * the server, up to MSGSIZE. The client is responsible for freeing
 * the message buffer.
 */
int ic_rpc_hello(struct ic_context *icc, int *retcode, char **retmsg);

#endif
