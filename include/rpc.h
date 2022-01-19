#ifndef _ADMIRE_IC_RPC_H
#define _ADMIRE_IC_RPC_H

#include <stdlib.h>            /* getenv   */
#include <margo.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "icc_priv.h"
#include "icc.h"

#define HG_PROTOCOL "ofi+tcp"	  /* Mercury protocol */
#define MARGO_PROVIDER_DEFAULT 0  /* for using multiple Argobots
				     pools, unused for now */

#define RPC_OK  0
#define RPC_ERR -1

#define RPC_E2BIG 2


/**
 * Get the Mercury (Hg) address string from the Margo server instance
 * MID and write it to ADDR_STR.
 *
 * Returns 0 if everything went fine, -1 otherwise.
 */
int
get_hg_addr(margo_instance_id mid, char *addr_str, hg_size_t *addr_str_size);


/**
 * Return a path to the file storing the ICC address. The caller is
 * responsible for freeing it.
 *
 * The returned path will be NULL if the memory alllocation went
 * wrong.
 */
char *
icc_addr_file(void);


/**
 * Translate from icc_log_level to margo_log_level.
 */
static inline margo_log_level
icc_to_margo_log_level(enum icc_log_level icc_log_level)
{
  switch (icc_log_level) {
  case ICC_LOG_EXTERNAL:
    return MARGO_LOG_EXTERNAL;
  case ICC_LOG_TRACE:
    return MARGO_LOG_TRACE;
  case ICC_LOG_DEBUG:
    return MARGO_LOG_DEBUG;
  case ICC_LOG_INFO:
    return MARGO_LOG_INFO;
  case ICC_LOG_WARNING:
    return MARGO_LOG_WARNING;
  case ICC_LOG_ERROR:
    return MARGO_LOG_ERROR;
  case ICC_LOG_CRITICAL:
    return MARGO_LOG_CRITICAL;
  default:
    return MARGO_LOG_ERROR;
  }
}


/**
 * Shorthand to set the id and callbacks arrays before registering the
 * RPC.
 */
#define REGISTER_PREP(ids,callbacks,idx,cb) ids[idx] = 1; callbacks[idx]=cb;

/**
 * Internal callback type. This is the expected signature of the
 * functions used in the custom registration process in the icc_rpc
 * module.
 */
typedef void (*icc_callback_t)(hg_handle_t h, margo_instance_id mid);


/**
 * RPC codes. Codes 1 to 127 are reserved for
 * internal use.
 *
 * ICC_RPC_CLIENT_REGISTER: Register an IC client as a Margo
 * "target". Once registered, and IC client can both send and receive
 * RPC server. The client needs to be started in MARGO_SERVER_MODE to
 * have an address.
 *
 * ICC_RPC_CLIENT_DEREGISTER: Deregister an IC client.
 *
 * For the public RPCs, see the functions documentation in icc.h.
 */
enum icc_rpc_code {
  RPC_ERROR = 0,

  /* internal RPCs */
  RPC_CLIENT_REGISTER,
  RPC_CLIENT_DEREGISTER,

  /* public RPCs */
  RPC_TEST = 128,
  RPC_ADHOC_NODES,
  RPC_JOBMON_SUBMIT,
  RPC_JOBMON_EXIT,
  RPC_MALLEABILITY_AVAIL,
  RPC_FLEXMPI_MALLEABILITY,

  RPC_COUNT
};

/**
 * Mercury-generated input/output RPC structs. Each input struct is
 * associated with one of the RPC identified by a name RPC_xx_NAME.
 */

/* Generic output struct */
MERCURY_GEN_PROC(rpc_out_t, ((int64_t)(rc)))


#define RPC_CLIENT_REGISTER_NAME  "icc_client_register"
#define RPC_CLIENT_DEREGISTER_NAME  "icc_client_deregister"

MERCURY_GEN_PROC(client_register_in_t,
                 ((hg_const_string_t)(clid))
                 ((hg_const_string_t)(type))
                 ((hg_uint32_t)(jobid))
                 ((hg_uint32_t)(jobntasks))
                 ((hg_uint32_t)(jobnnodes))
                 ((hg_const_string_t)(addr_str))
                 ((hg_uint16_t)(provid)))

MERCURY_GEN_PROC(client_deregister_in_t,
                 ((hg_const_string_t)(clid)))


#define RPC_TEST_NAME  "icc_test"

MERCURY_GEN_PROC(test_in_t,
                 ((hg_const_string_t)(clid))
                 ((hg_const_string_t)(type))
                 ((hg_uint32_t)(jobid))
                 ((uint8_t)(number)))


#define RPC_JOBMON_SUBMIT_NAME  "icc_jobmon_submit"
#define RPC_JOBMON_EXIT_NAME  "icc_jobmon_exit"

MERCURY_GEN_PROC(jobmon_submit_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(jobstepid))
                 ((uint32_t)(nnodes)))

MERCURY_GEN_PROC(jobmon_exit_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(jobstepid)))


#define RPC_ADHOC_NODES_NAME  "icc_adhoc_nodes"

MERCURY_GEN_PROC(adhoc_nodes_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(nnodes))
                 ((uint32_t)(adhoc_nnodes)))


#define RPC_MALLEABILITY_AVAIL_NAME  "icc_malleability_avail"

MERCURY_GEN_PROC(malleability_avail_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((hg_const_string_t)(type))
                 ((hg_const_string_t)(portname))
                 ((uint32_t)(nnodes)))


#define RPC_FLEXMPI_MALLEABILITY_NAME  "icc_flexmpi_malleability"

MERCURY_GEN_PROC(flexmpi_malleability_in_t,
                 ((hg_const_string_t)(command)))


/**
 * Send RPC identifed by RPC_CODE from Margo instance MID to the Margo
 * provider identified by ADDR with input struct DATA.
 *
 * Returns 0 or -1 in case of error. RETCODE is filled with the RPC
 * return code.
 */
int
rpc_send(margo_instance_id mid, hg_addr_t addr, hg_id_t rpc_id,
         void *data, int *retcode);

#endif