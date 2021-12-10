#ifndef _ADMIRE_ICC_H
#define _ADMIRE_ICC_H

#include <stdint.h>             /* uintXX_t */

#define ICC_MAJOR 1
#define ICC_MINOR 0
#define ICC_PATCH 0


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

struct app_rpc_test_in {
  const char * instruction;

};


/**
 * Initialize an ICC client instance and returns the associated
 * context in ICC_CONTEXT.
 *
 * If argument BIDIRECTIONAL is non-zero, arrange that this ICC client
 * can receive RPCs from the intelligent controller, not just send
 * them.
 *
 * Return ICC_SUCCESS or error code.
 */
int icc_init(enum icc_log_level log_level, int bidirectional, struct icc_context **icc);


/**
 * Finalize the ICC client instance associated with the context passed as argument.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_fini(struct icc_context *icc);


/**
 * RPC TEST: Test the server by sending a number and having it logged.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_test(struct icc_context *icc, uint8_t number, int *retcode);


/**
 * RPC ADHOC_NODES: Request ADHOC_NODES nodes for the job identified by JOBID that has
 * been assigned NNODES in total.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_adhoc_nodes(struct icc_context *icc, uint32_t jobid, uint32_t nnodes, uint32_t adhoc_nnodes, int *retcode);


/**
 * RPC: Notify the IC that the job JOBID.JOBSTEPID has been submitted,
 * requesting NNODES nodes.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_jobmon_submit(struct icc_context *icc, uint32_t jobid, uint32_t jobstepid, uint32_t nnodes, int *retcode);


/**
 * RPC: Notify the IC that the job JOBID.JOBSTEPID has exited,
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_jobmon_exit(struct icc_context *icc, uint32_t jobid, uint32_t jobstepid, int *retcode);


/**
 * RPC MALLEABILITY_AVAIL: Notify the server of the availability of
 * NNODES for later malleability commands to job JOBID, of type
 * TYPE. The available processes can be contacted at PORTNAME.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_malleability_avail(struct icc_context *icc, char *type, char *portname, uint32_t jobid, uint32_t nnodes, int *retcode);

#endif
