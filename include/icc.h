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
  ICC_RPC_ERROR = 0,
  /* RPC code 1 to 127 reserved for internal use */
  ICC_RPC_PRIVATE = 128,
  ICC_RPC_TEST,
  APP_RPC_TEST,
  ICC_RPC_JOBMON_SUBMIT,
  ICC_RPC_JOBMON_EXIT,
  ICC_RPC_ADHOC_NODES,
  ICC_RPC_COUNT
};

struct icc_rpc_test_in {
  uint8_t number;
};

struct app_rpc_test_in {
  const char * instruction;
};


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
int icc_init_opt(enum icc_log_level log_level, struct icc_context **icc, int server_id);


/**
 * Finalize the ICC client instance associated with the context passed as argument.
 *
 * Return ICC_SUCCESS or an error code.
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
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_send(struct icc_context *icc, enum icc_rpc_code icc_code, void *data_in, int *retcode);


/**
 * Register the RPC identified by ICC_RPC_CODE.
 *
 * Return ICC_SUCCESS or an error code.
 */
int icc_rpc_register(struct icc_context *icc, enum icc_rpc_code icc_code);


#endif
