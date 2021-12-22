#ifndef _ADMIRE_ICC_RPC_H
#define _ADMIRE_ICC_RPC_H

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
 * ICC_RPC_TARGET_REGISTER: Register an IC "target". Once registered,
 * and IC client can both send and receive RPC server. The client
 * needs to be started in MARGO_SERVER_MODE to have an address.
 *
 * ICC_RPC_TARGET_DEREGISTER: Deregister an IC "target".
 *
 * For the public RPCs, see the functions documentation in icc.h.
 */
enum icc_rpc_code {
  ICC_RPC_ERROR = 0,

  /* internal RPCs */
  ICC_RPC_TARGET_REGISTER,
  ICC_RPC_TARGET_DEREGISTER,

  /* public RPCs */
  ICC_RPC_TEST = 128,
  ICC_RPC_ADHOC_NODES,
  ICC_RPC_JOBMON_SUBMIT,
  ICC_RPC_JOBMON_EXIT,
  ICC_RPC_MALLEABILITY_AVAIL,
  ICC_RPC_FLEXMPI_MALLEABILITY,

  ICC_RPC_COUNT
};

/**
 * Mercury-generated structs. Each struct is associated with one of
 * the RPC identified by an icc_rpc_code (check the name for
 * correspondance).
 */

/* Generic output struct */
MERCURY_GEN_PROC(rpc_out_t, ((int64_t)(rc)))

MERCURY_GEN_PROC(target_register_in_t,
                 ((hg_const_string_t)(clid))
                 ((hg_uint32_t)(jobid))
                 ((hg_const_string_t)(type))
                 ((hg_const_string_t)(addr_str))
                 ((hg_uint16_t)(provid)))

MERCURY_GEN_PROC(target_deregister_in_t,
                 ((hg_const_string_t)(clid)))

MERCURY_GEN_PROC(test_in_t,
                 ((hg_const_string_t)(clid))
                 ((hg_uint32_t)(jobid))
                 ((uint8_t)(number)))

MERCURY_GEN_PROC(adhoc_nodes_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(nnodes))
                 ((uint32_t)(adhoc_nnodes)))

MERCURY_GEN_PROC(jobmon_submit_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(jobstepid))
                 ((uint32_t)(nnodes)))

MERCURY_GEN_PROC(jobmon_exit_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((uint32_t)(jobstepid)))

MERCURY_GEN_PROC(malleability_avail_in_t,
                 ((hg_const_string_t)(clid))
                 ((uint32_t)(jobid))
                 ((hg_const_string_t)(type))
                 ((hg_const_string_t)(portname))
                 ((uint32_t)(nnodes)))

MERCURY_GEN_PROC(flexmpi_malleability_in_t,
                 ((hg_const_string_t)(command)))


/**
 * Register RPCs to Margo instance MID. If the id in IDS is 0, the
 * corresponding RPC will not be registered. If it is not 0, the
 * RPC will be registered with the callback given in CALLBACKS, or
 * with NULL if no callback is given.
 *
 * Once registered the Mercury id of the RPC will be placed in the IDS
 * array.
 *
 * Returns 0 if the registrations went fine, -1 otherwise.
 *
 * WARNING:
 * The function pointers in CALLBACKS will be passed to RPCs handler,
 * the caller must make sure that they don’t go out of scope before
 * margo_finalize is called (how would a function go out of scope
 * though?).
 */
int
register_rpcs(margo_instance_id mid, icc_callback_t callbacks[ICC_RPC_COUNT], hg_id_t ids[ICC_RPC_COUNT]);


/**
 * Send RPC identifed by RPC_CODE from Margo instance MID to the Margo
 * provider identified by ADDR & PROVID with input struct DATA.
 *
 * Returns 0 or -1 in case of error. RETCODE is filled with the RPC
 * return code.
 */
int
rpc_send(margo_instance_id mid, hg_addr_t addr, uint16_t provid, hg_id_t rpc_id,
         void *data, int *retcode);

#endif
