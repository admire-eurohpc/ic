#ifndef _ADMIRE_ICC_H
#define _ADMIRE_ICC_H

#include <stdint.h>

struct icc_context;

#define ICC_SUCCESS  0
#define ICC_FAILURE -1


/* Log levels lifted from Margo */
enum icc_log_level {
    ICC_LOG_EXTERNAL,
    ICC_LOG_TRACE,
    ICC_LOG_DEBUG,
    ICC_LOG_INFO,
    ICC_LOG_WARNING,
    ICC_LOG_ERROR,
    ICC_LOG_CRITICAL
};

enum icc_rpc {
  ICC_RPC_ERROR,
  ICC_RPC_HELLO,
  ICC_RPC_ADHOC_NODES,
  ICC_RPC_COUNT
};

struct icc_rpc_ret {
  int  retcode;
  char *msg;
};


/**
 * Initialize a Margo client context.
 * Return ICC_SUCCESS or error code.
*/
int icc_init(enum icc_log_level log_level, struct icc_context **icc);

/**
 * Finalize the Margo instance associated with icc_context ICC.
 */
int icc_fini(struct icc_context *icc);

/**
 * Make the "hello" RPC.
 *
 * Fill RETCODE and RETMSG with the the code and return message from
 * the server, up to MSGSIZE. The client is responsible for freeing
 * the message buffer.
 */
int icc_rpc_hello(struct icc_context *icc, int *retcode, char **retmsg);

/**
 * "adhoc_nodes" RPC.
 *
 * Request ADHOC_NODES for the slurm job identified by SLURM_JOBID
 * that has been assigned SLURM_NNODES in total.
 *
 * Fill RETCODE on completion.
 */
int icc_rpc_adhoc_nodes(struct icc_context *icc,
		       uint32_t slurm_jobid,
		       uint32_t slurm_nnodes,
		       uint32_t adhoc_nnodes,
		       int *retcode);

#endif
