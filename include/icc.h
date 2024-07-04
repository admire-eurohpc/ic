#ifndef _ADMIRE_ICC_H
#define _ADMIRE_ICC_H

#include <stdbool.h>
#include <stdint.h>             /* uintxx_t */

#define ICC_MAJOR 1
#define ICC_MINOR 0


struct icc_context;

enum icc_retcode {
  ICC_FAILURE = -1,
  ICC_SUCCESS = 0,
  ICC_ENOMEM,
  ICC_EOVERFLOW,
  ICC_EINVAL,
};
typedef enum icc_retcode iccret_t;

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
  ICC_TYPE_STOPRESTART,
  ICC_TYPE_JOBCLEANER,
  ICC_TYPE_JOBMON,
  ICC_TYPE_ADHOCCLI,
  ICC_TYPE_IOSETS,
  ICC_TYPE_RECONFIG2,
  ICC_TYPE_ALERT,
  ICC_TYPE_COUNT,
};

enum icc_reconfig_type {
  ICC_RECONFIG_NONE,
  ICC_RECONFIG_EXPAND,
  ICC_RECONFIG_SHRINK
};

/**
 * Expected signature of the function that libicc calls on receiving a
 * reconfiguration RPC.
 *
 * SHRINK will be set if the IC requests a reduction in the number of
 * resources. SHRINK, MAXPROCS and HOSTLIST are provided by the IC,
 * DATA is a pointer passed at registration by the caller, and passed
 * back as-is to the function by libicc.
 *
 * Note: libicc makes sure that only one Argobot (~thread) calls this
 * function at a time, so it doesn’t need to be thread-safe nor
 * reentrant.
 */
typedef int (*icc_reconfigure_func_t)(int shrink, uint32_t maxprocs, const char *hostlist, void *data);


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
 * RUN_MODE 0 for initial execution and 1 for restarted app
 *
 * Return ICC_SUCCESS or an error code.
 */
/*int icc_init_mpi(enum icc_log_level log_level, enum icc_client_type typeid, int nprocs, icc_reconfigure_func_t func, void *data, struct icc_context **icc);*/
int icc_init_mpi(enum icc_log_level log_level, enum icc_client_type typeid, int nprocs, icc_reconfigure_func_t func, void *data, int run_mode, char ** ic_addr, char ** clid, const char *hostlist, struct icc_context **icc);

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
int icc_wait_for_finalize(struct icc_context *icc);


/**
 * Set RECONFIGTYPE to ICC_RECONFIG_EXPAND if there is a pending expansion
 * order. In this case NPROCS and HOSTLIST will be set to indicate
 * where the expansion should take place.
 *
 * Set RECONFIGTYPE to ICC_RECONFIG_SHRINK if there is a pending shrinking
 * order. In this case NPROCS will be set to indicate the number of
 * CPUs to release.
 *
 * Set RECONFIGTYPE to ICC_RECONFIG_NONE if there is no pending order.
 *
 * Return ICC_SUCCESS or an error code.
 */
iccret_t icc_reconfig_pending(struct icc_context *icc,
                              enum icc_reconfig_type *reconfigtype,
                              uint32_t *nprocs, const char **hostlist);

/**
 * Set LOWMEM to true if the controller reported a low memory condition
 *
 * Return ICC_SUCCESS or an error code.
 */
iccret_t icc_lowmem_pending(struct icc_context *icc, bool *lowmem);

/**
 * Register NCPUS on HOST for release. The resources will be actually
 * released to the resource manager when calling icc_release_nodes()
 * (which see).
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_release_register(struct icc_context *icc, const char *host, uint16_t ncpus);


/**
 * Release nodes that have been registered for release by
 * icc_release_register() to the resource manager.
 */
int icc_release_nodes(struct icc_context *icc);


/**
 * Inform the IC of the beginning of an IO slice. WITE is the
 * IO-set characteristic time of the application, in seconds.
 *
 * Set the ISFIRST flag if it is the first slice in an IO phase.

 * Returns when no other application in the same IO-set is running,
 * setting NSLICES to the number of slices the application is allowed
 * to write before having to ask for permission again.
 */
iccret_t icc_hint_io_begin(struct icc_context *icc, unsigned long witer,
                           int isfirst, unsigned int *nslices);


/**
 * Inform the IC of the end of an IO slice. WITER is the IO-set
 * characteristic time of the application in seconds. Set the
 * ISLAST flag if it is the last slice in an IO phase.
 */
iccret_t icc_hint_io_end(struct icc_context *icc, unsigned long witer, int islast, unsigned long long nbytes);


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
 * Malleability notification type.
 * WARNING: must fit in uint8.
 */
enum icc_malleability_region_action {
  ICC_MALLEABILITY_UNDEFINED = 0,
  ICC_MALLEABILITY_REGION_ENTER,
  ICC_MALLEABILITY_REGION_LEAVE,
  ICC_MALLEABILITY_LIM
};

/**
 * RPC MALLEABILITY_REGION: Notify the server of the start (if type is
 * ICC_MALLEABILITY_REGION_ENTER) or end (if type is
 * ICC_MALLEABILITY_REGION_LEAVE) of a malleability region.
 *
 * procs_hint requests a specific number of processors, exclusive_hint
 * indicates whether one node per processor is desired.
 * NOTE: a "number of nodes" hint is probably more appropriate.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
/* CHANGE: begin */
/* procs_hint is a hint of the num. of procs to remove/add */
/* excl_nodes_hint is a hint of using one node per proc. or not */
//int icc_rpc_malleability_region(struct icc_context *icc, enum icc_malleability_region_action type, int *retcode);
int icc_rpc_malleability_region(struct icc_context *icc, enum icc_malleability_region_action type, int procs_hint, int exclusive_hint, int *retcode);
/* CHANGE: end */

/**
 * Alerts
 */
enum icc_alert_type {
  ICC_ALERT_UNDEFINED = 0,
  ICC_ALERT_IO,
  ICC_ALERT_LEN
};

/**
* Send a complete Metric Alert to the IC
* SOURCE: the name of the emitting collector (jobid, Node Id or main (all))
* NAME: Name of the alarm
* METRIC: Name of the blamed metric
* OPERATOR: Name of the applied operator in the alarm
* CURRENT_VALUE: value of the given metric
* ACTIVE: true if the alarm is currently active alarms are only notified on value change
* PRETTY_PRINT: a string containing a clean description of the alarm
*
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
*/
int icc_rpc_metric_alert(struct icc_context * icc,  char * source, char * name, char * metric, char * operator, double current_value, int active, char * pretty_print, int *retcode );


/**
 * Send an alert of type TYPE to the controller.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_alert(struct icc_context *icc, enum icc_alert_type type, int *retcode);

/**
 * Node alert
 */

/**
 * Send an alert of type TYPE about NODE to the controller.
 *
 * RETCODE is filled with the RPC return status code on completion.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_nodealert(struct icc_context *icc, enum icc_alert_type type, const char *node, int *retcode);

/*ALBERTO 26062023 */
/**
 * Register NCPUS on HOST for release. The resources will be actually
 * released to the resource manager when calling icc_release_nodes()
 * (which see).
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_remove_node(struct icc_context *icc, const char *host, uint16_t ncpus);

/**
 * RPC checkpointing: application asks the IC if it has to execute a checkpoint phase..
 *
 * RETCODE is filled with 0 if no checkpoint needed, 1 otherwhise. .
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_checkpointing();

/**
 * RPC malleability query: application asks the IC for the malleability decision
 *
 * RETURN if malleability, nnodes to add/remove, and nodelist to add/remove
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_malleability_query(struct icc_context *icc, int *malleability, int *nnodes, char **nodelist);
/*END ALBERTO 26062023*/

/*ALBERTO 05092023*/
/**
 * RPC malleability query: application asks the IC for the malleability decision
 *
 * RETURN if malleability, nnodes to add/remove, and nodelist to add/remove
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_malleability_ss(struct icc_context *icc, int *retcode);

/**
 * Backup relevant data from icc_context to file
*/
void _icc_context_backup(struct icc_context *icc, char *filename);

/**
 * Load relevant data from binary file to icc_context
*/
void _icc_context_load(struct icc_context *icc, char *filename);

#endif
