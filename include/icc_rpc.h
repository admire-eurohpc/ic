#ifndef _ADMIRE_ICC_RPC_H
#define _ADMIRE_ICC_RPC_H

#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "icc.h"

#define ICC_ADDR_MAX_SIZE 128
#define ICC_ADDR_FILE "/tmp/ic.addr"
#define ICC_HG_PROVIDER "ofi+tcp"
#define ICC_RPC_TIMEOUT_MS 10000

/* Margo provider != Hg network provider */
#define ICC_MARGO_PROVIDER_ID_DEFAULT 12

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


MERCURY_GEN_PROC(hello_out_t,
                 ((int64_t)(rc)) ((hg_string_t)(msg)))


/* Ad-hoc storage --adm-adhoc-nodes option */
MERCURY_GEN_PROC(adhoc_nodes_in_t,
		 ((uint32_t)(slurm_jobid))
		 ((uint32_t)(slurm_nnodes))
		 ((uint32_t)(adhoc_nnodes)))

MERCURY_GEN_PROC(adhoc_nodes_out_t, ((int64_t)(rc)))



#endif
