#ifndef ADMIRE_IC_RPC_H
#define ADMIRE_IC_RPC_H

#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "ic.h"

#define IC_ADDR_MAX_SIZE 128
#define IC_ADDR_FILE "/tmp/ic.addr"
#define IC_HG_PROVIDER "ofi+tcp"

/**
 * Translate from ic_log_level to margo_log_level.
 */
static inline margo_log_level
ic_to_margo_log_level(enum ic_log_level ic_log_level)
{
  switch (ic_log_level) {
  case IC_LOG_EXTERNAL:
    return MARGO_LOG_EXTERNAL;
  case IC_LOG_TRACE:
    return MARGO_LOG_TRACE;
  case IC_LOG_DEBUG:
    return MARGO_LOG_DEBUG;
  case IC_LOG_INFO:
    return MARGO_LOG_INFO;
  case IC_LOG_WARNING:
    return MARGO_LOG_WARNING;
  case IC_LOG_ERROR:
    return MARGO_LOG_ERROR;
  case IC_LOG_CRITICAL:
    return MARGO_LOG_CRITICAL;
  default:
    return MARGO_LOG_ERROR;
  }
}


MERCURY_GEN_PROC(hello_out_t,
		 ((int64_t)(rc)) ((hg_string_t)(msg)))


#endif
