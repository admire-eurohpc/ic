#include <errno.h>
#include <inttypes.h>           /* PRId32 */
#include <stddef.h>             /* NULL */
#include <stdint.h>             /* uint32_t, etc. */
#include <stdlib.h>             /* strtol */
#include <string.h>             /* strerror */
#include <slurm/spank.h>

#include "icc.h"

SPANK_PLUGIN(job-monitor, 1)

struct jobmonctx {
  uint32_t jobid;
  uint32_t jobstepid;
  uint32_t nnodes;
};

struct jobmonctx jmctx = {.jobid = 0, .jobstepid = 0, .nnodes = 0};


/**
 * Return 1 if the ADMIRE plugin should be used.
 */
static int
adm_enabled() {
  char *adm_enabled = getenv("ADMIRE_ENABLE");
  if (!adm_enabled || !strcmp(adm_enabled, "") || !strcmp(adm_enabled, "0")
      || !strcmp(adm_enabled, "no") || !strcmp(adm_enabled, "NO"))
    return 0;
  else
    return 1;
}


/**
 * Called locally in srun, after jobid & stepid are available.
 */
int
slurm_spank_local_user_init(spank_t sp,
                            int ac __attribute__((unused)),
                            char **av __attribute__((unused)))
{
  if (!adm_enabled())
    return 0;

  spank_err_t sprc;

  sprc = spank_get_item(sp, S_JOB_ID, &jmctx.jobid);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error ("ADMIRE jobmon: Failed to get jobid: %s.", spank_strerror(sprc));
  }

  sprc = spank_get_item(sp, S_JOB_STEPID, &jmctx.jobstepid);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error ("ADMIRE jobmon: Failed to get jobstepid: %s.", spank_strerror(sprc));
  }

  sprc = spank_get_item(sp, S_JOB_NNODES, &jmctx.nnodes);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error ("ADMIRE jobmon: Failed to get job nnodes: %s.", spank_strerror(sprc));
  }

  struct icc_context *icc;
  int rpc_retcode;
  int rc;

  rc = icc_init(ICC_LOG_INFO,0, &icc);
  if (rc != ICC_SUCCESS) {
    slurm_error("ADMIRE jobmon: Could not initialize connection to IC: %d", rc);
    return -1;
  }

  struct icc_rpc_jobmon_submit_in in = {
    .slurm_jobid=jmctx.jobid,
    .slurm_jobstepid=jmctx.jobstepid,
    .slurm_nnodes=jmctx.nnodes,
  };

  rc = icc_rpc_send(icc, ICC_RPC_JOBMON_SUBMIT, &in, &rpc_retcode);
  if (rc == ICC_SUCCESS) {
    slurm_info("RPC jobmon_submit successful: retcode=%d", rpc_retcode);
  } else {
    slurm_error("Error making RPC to IC (retcode=%d)", rc);
  }

  rc = icc_fini(icc);
  if (rc != ICC_SUCCESS)
    slurm_error("ADMIRE jobmon: Could not destroy IC context");

  return 0;
}


/**
 * In local context, called just before srun exits.
 */
int
slurm_spank_exit(spank_t sp,
                 int ac __attribute__((unused)), char **av __attribute__((unused)))
{
  if (!adm_enabled())
    return 0;

  /* run in local (srun) context only */
  if (spank_remote(sp))
    return 0;

  struct icc_context *icc;
  int rpc_retcode;
  int rc;

  rc = icc_init(ICC_LOG_INFO, 0, &icc);
  if (rc != ICC_SUCCESS) {
    slurm_error("ADMIRE jobmon: Could not initialize connection to IC: %d", rc);
    return 0;
  }

  struct icc_rpc_jobmon_exit_in in = {
    .slurm_jobid=jmctx.jobid,
    .slurm_jobstepid=jmctx.jobstepid,
  };

  rc = icc_rpc_send(icc, ICC_RPC_JOBMON_EXIT, &in, &rpc_retcode);
  if (rc == ICC_SUCCESS) {
    slurm_info("RPC jobmon_exit successful: retcode=%d", rpc_retcode);
  } else {
    slurm_error("Error making RPC to IC (retcode=%d)", rc);
  }

  rc = icc_fini(icc);
  if (rc != ICC_SUCCESS)
    slurm_error("ADMIRE jobmon: Could not destroy IC context");

  return 0;
}
