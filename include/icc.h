#ifndef _ADMIRE_ICC_H
#define _ADMIRE_ICC_H

#include <stdint.h>             /* uintxx_t */

#define ICC_MAJOR 1
#define ICC_MINOR 0


struct icc_context;

#define ICC_SUCCESS  0
#define ICC_FAILURE -1
#define ICC_ENOMEM -1


/* Log levels, lifted from Margo */
enum icc_log_level {
  ICC_LOG_EXTERNAL,
  ICC_LOG_TRACE,
  ICC_LOG_DEBUG,
  ICC_LOG_INFO,
  ICC_LOG_WARNING,
  ICC_LOG_ERROR,
  ICC_LOG_CRITICAL
};

/**
 * Type of IC client. One of those must be passed to the icc_init
 * function.
 */
enum icc_client_type {
  ICC_TYPE_UNDEFINED,
  ICC_TYPE_MPI,
  ICC_TYPE_FLEXMPI,
  ICC_TYPE_JOBCLEANER,
  ICC_TYPE_JOBMON,
  ICC_TYPE_ADHOCCLI,
  ICC_TYPE_COUNT,
};


/**
 * Expected signature of the function that libicc calls on receiving a
 * reconfiguration RPC.
 *
 * MAXPROCS and HOSTLIST is provided by the IC, DATA is a pointer
 * passed at registration by the caller, and passed back as-is to the
 * function by libicc.
 */
typedef int (*icc_reconfigure_func_t)(int maxprocs, const char *hostlist, void *data);


/**
 * Initialize an ICC client instance and returns the associated
 * context in ICC_CONTEXT. TYPEID is the type of ICC client.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_init(enum icc_log_level log_level, enum icc_client_type typeid, struct icc_context **icc);


/**
 * Initialize an ICC MPI client instance and returns the associated
 * context in ICC_CONTEXT.
 *
 * See icc_init. In addition, NPROCS is the number of processes in
 * this client, returned for example by MPI_Comm_size. Also register
 * FUNC to be called by libicc on receiving a reconfiguration RPC with
 * DATA passed back as-is to FUNC.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_init_mpi(enum icc_log_level log_level, enum icc_client_type typeid, unsigned int nprocs, icc_reconfigure_func_t func, void *data, struct icc_context **icc);


/**
 * Finalize the ICC client instance associated with the context passed as argument.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_fini(struct icc_context *icc);


/**
 * Suspend the ICC client for TIMEOUT_MS. Call this instead of
 * sleep(3) to avoid blocking a Margo ULT.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_sleep(struct icc_context *icc, double timeout_ms);


/**
 * Suspend (and don’t block) the calling thread until some other
 * entity (e.g. another ULT, or a signal handler) invokes icc_fini().
 *
 * Return ICC_SUCCESS or an error code.
 */
int
icc_wait_for_finalize(struct icc_context *icc);


/**
 * RPC TEST: Test the server by sending a number and having it logged.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_test(struct icc_context *icc, uint8_t number, enum icc_client_type type, int *retcode);


/**
 * RPC JOBCLEAN: Instruct the IC to clean the data structures
 * associated with JOBID.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_jobclean(struct icc_context *icc, uint32_t jobid, int *retcode);


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


/**
 * Malleability notification.
 */
enum icc_malleability_region_action {
  ICC_MALLEABILITY_UNDEFINED = 0,
  ICC_MALLEABILITY_REGION_ENTER,
  ICC_MALLEABILITY_REGION_LEAVE
};

/**
 * RPC MALLEABILITY_REGION: Notify the server of the start (if type is
 * ICC_MALLEABILITY_REGION_ENTER) or end (if type is
 * ICC_MALLEABILITY_REGION_LEAVE) of a malleability region..
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_malleability_region(struct icc_context *icc, enum icc_malleability_region_action type, int *retcode);


#endif
