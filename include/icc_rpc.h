#ifndef _ADMIRE_ICC_RPC_H
#define _ADMIRE_ICC_RPC_H

#include <stdlib.h>            /* getenv   */
#include <margo.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "icc.h"

#define ICC_ADDR_FILENAME "icc.addr"
#define ICC_ADDR_MAX_SIZE 128
#define ICC_HG_PROVIDER "ofi+tcp"
#define ICC_RPC_TIMEOUT_MS 10000

/* Margo provider != Hg network provider */
#define ICC_MARGO_PROVIDER_ID_DEFAULT 12

/**
 * Get the Mercury (Hg) address string from the Margo server instance
 * MID and write it to ADDR_STR.
 *
 * Returns 0 if everything went fine, -1 otherwise.
 */
int
icc_hg_addr(margo_instance_id mid, char *addr_str, hg_size_t *addr_str_size);


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
 * Shorthand to set the id and callbacks arrays before registering the
 * RPC.
 */
#define ICC_RPC_PREPARE(ids,callbacks,idx,cb) ids[idx] = 1; callbacks[idx]=cb;

typedef void (*icc_callback_t)(hg_handle_t h);

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
 * margo_finalize is called.
 */
int
register_rpcs(margo_instance_id mid, icc_callback_t callbacks[ICC_RPC_COUNT], hg_id_t ids[ICC_RPC_COUNT]);


/**
 * Return a path to the file storing the ICC address. The caller is
 * responsible for freeing it.
 * @param Identifier for the component (margo_server) (described in root_connections.h)
 */
static inline char *
icc_addr_file_opt(int server_id) {
  const char *runtimedir = getenv("ADMIRE_DIR");
  if (!runtimedir)
    runtimedir = getenv("HOME");
  if (!runtimedir)
    runtimedir = ".";

  char *path = (char*)malloc(strlen(runtimedir) + strlen(ICC_ADDR_FILENAME) + 3);
  if (path) {
    sprintf(path, "%s/%s%d", runtimedir, ICC_ADDR_FILENAME, server_id);
    printf("OPENING PATH: %s\n", path);
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

MERCURY_GEN_PROC(malleabilityman_in_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(malleabilityman_out_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(slurmman_in_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(slurmman_out_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(iosched_out_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(adhocman_out_t, ((uint8_t)(number)))

MERCURY_GEN_PROC(monitor_out_t, ((uint8_t)(number)))

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
