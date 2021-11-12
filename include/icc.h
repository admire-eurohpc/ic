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

/**
 * ICC RPC codes.
 *
 * ICC_RPC_TEST: test the server by sending a number and having it
 * logged.
 *
 * ICC_RPC_ADHOC_NODES: Request ADHOC_NODES for the slurm job
 * identified by SLURM_JOBID that has been assigned SLURM_NNODES in
 * total.
 *
 * Fill RETCODE on completion.
 */
enum icc_rpc_code {
  ICC_RPC_ERROR,
  ICC_RPC_TEST,
  ICC_RPC_MALLEABILITY,
  ICC_RPC_SLURM,
  ICC_RPC_IOSCHED,
  ICC_RPC_ADHOC,
  ICC_RPC_MONITOR,
  ICC_RPC_JOBMON_SUBMIT,
  ICC_RPC_JOBMON_EXIT,
  ICC_RPC_ADHOC_NODES,
  ICC_RPC_COUNT
};

struct icc_rpc_test_in {
  uint8_t number;
};

/* Structs for RPC communications between Root Controller and other components*/
struct icc_rpc_malleability_manager_in{
    uint8_t number;
};

struct icc_rpc_slurm_manager_in{
    uint8_t number;
};

struct icc_rpc_io_scheduler_in{
    uint8_t number;
};

struct icc_rpc_adhoc_manager_in{
    uint8_t number;
};

struct icc_rpc_monitoring_manager_in{
    uint8_t number;
};
/* End of root controller rpc structs*/



struct icc_rpc_jobmon_submit_in {
  uint32_t slurm_jobid;
  uint32_t slurm_jobstepid;
  uint32_t slurm_nnodes;
};

struct icc_rpc_jobmon_exit_in {
  uint32_t slurm_jobid;
  uint32_t slurm_jobstepid;
};

struct icc_rpc_adhoc_nodes_in {
  uint32_t slurm_jobid;
  uint32_t slurm_nnodes;
  uint32_t adhoc_nnodes;
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
 * Generic RPC call to the Intelligent Controller.
 *
 * The RPC is identified by the ICC_CODE, see enum icc_rpc_code.
 *
 * DATA_IN is a pointer to a structure which depends on the RPC type
 *
 * RETCODE is filled with the RPC return status code on completion.
 */
int icc_rpc_send(struct icc_context *icc, enum icc_rpc_code icc_code, void *data_in, int *retcode);


#endif
