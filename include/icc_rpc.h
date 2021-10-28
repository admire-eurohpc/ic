#ifndef _ADMIRE_ICC_RPC_H
#define _ADMIRE_ICC_RPC_H

#include <stdlib.h>            /* getenv   */
#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "icc.h"

#define IC_RUNTIME_DIR_DEFAULT "."

#define ICC_ADDR_FILENAME "icc.addr"
#define ICC_ADDR_MAX_SIZE 128
#define ICC_HG_PROVIDER "ofi+tcp"
#define ICC_RPC_TIMEOUT_MS 10000

/* Margo provider != Hg network provider */
#define ICC_MARGO_PROVIDER_ID_DEFAULT 12


/**
 * Return a path to the file storing the ICC address. The caller is
 * responsible for freeing it
 */
static inline char *
icc_addr_file() {
  char *runtimedir = getenv("IC_RUNTIME_DIR");
  if (!runtimedir)
    runtimedir = getenv("HOME");
  if (!runtimedir)
    runtimedir = ".";

  char *path = malloc(strlen(runtimedir) + strlen(ICC_ADDR_FILENAME) + 2);
  if (path) {
    sprintf(path, "%s/%s", runtimedir, ICC_ADDR_FILENAME);
  }
  return path;
}


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

/* Generic output struct */
MERCURY_GEN_PROC(rpc_out_t, ((int64_t)(rc)))


/* Mercury-generated struct
 *
 * /!\ Copied in the icc.h public header file, keep in sync!
 */
MERCURY_GEN_PROC(test_in_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(adhoc_nodes_in_t,
                 ((uint32_t)(slurm_jobid))
                 ((uint32_t)(slurm_nnodes))
                 ((uint32_t)(adhoc_nnodes)))

MERCURY_GEN_PROC(jobmon_submit_in_t,
                 ((uint32_t)(slurm_jobid))
                 ((uint32_t)(slurm_jobstepid))
                 ((uint32_t)(slurm_nnodes)))

MERCURY_GEN_PROC(jobmon_exit_in_t,
                 ((uint32_t)(slurm_jobid))
                 ((uint32_t)(slurm_jobstepid)))

#endif
