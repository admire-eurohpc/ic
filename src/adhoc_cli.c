#include <errno.h>
#include <inttypes.h>           /* PRId32 */
#include <stddef.h>             /* NULL */
#include <stdint.h>             /* uint32_t, etc. */
#include <stdlib.h>             /* strtol */
#include <string.h>             /* strerror */
#include <slurm/spank.h>

#include "ic.h"

SPANK_PLUGIN (adhoc-cli, 1)


static int adhoc_flag = 0;  /* if there are adhoc options */

/* Number of nodes requested for the Ad-hoc storage */
static uint32_t adhoc_nnodes = 0;
/* XX other adhoc_ options */

static int process_opts(int val, const char *optarg, int remote);

struct spank_option spank_options [] = {
  {
    "adm-adhoc-nodes",
    "[nbnodes]",
    "Dedicate [nbnodes] to the ad-hoc storage.",
    1,                           /* option takes an argument */
    0,                           /* XX value? */
    (spank_opt_cb_f)process_opts /* callback  */
  },
  SPANK_OPTIONS_TABLE_END
};


static int
process_opts(int val, const char *optarg, int remote)
{
  if (spank_context() != S_CTX_LOCAL)
    return 0;

  /* if we're here => some adhoc options were passed to the Slurm CLI */
  adhoc_flag = 1;

  adhoc_nnodes = strtol(optarg, NULL, 0);
  if (adhoc_nnodes == 0)
    return -1;

  slurm_info("ADMIRE: Requested %"PRId32" nodes for the ad-hoc FS (remote=%d, context=%d)", adhoc_nnodes, remote, spank_context());
  return 0;
}


/**
 * Called locally in srun, after jobid & stepid are available.
 */
int
slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
  if (!adhoc_flag)
    return 0;

  uint32_t jobid, nnodes;       /* Slurm jobid & assigned nodes */
  spank_err_t sprc;

  sprc = spank_get_item(sp, S_JOB_ID, &jobid);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error ("ADMIRE: Failed to get jobid %s:", spank_strerror(sprc));
    jobid = 0;
  }

  sprc = spank_get_item(sp, S_JOB_NNODES, &nnodes);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error ("ADMIRE: Failed to get job nnodes %s:", spank_strerror(sprc));
    nnodes = 0;
  }

  slurm_info("ADMIRE: Hello from %s (context=%d, jobid=%d)", __func__, spank_context(), jobid);

  struct ic_context *icc;
  int rpc_retcode;
  int rc;

  rc = ic_init(IC_LOG_INFO, &icc);
  if (rc != IC_SUCCESS) {
    slurm_error("ADMIRE: Could not initialize connection to IC");
    return -1;
  }

  rc = ic_rpc_adhoc_nodes(icc, jobid, nnodes, adhoc_nnodes, &rpc_retcode);
  if (rc == IC_SUCCESS) {
    slurm_info("RPC adhoc_nodes successful: retcode=%d", rpc_retcode);
  } else {
    slurm_error("Error making RPC to IC (retcode=%d)", rc);
  }

  rc = ic_fini(icc);
  if (rc != IC_SUCCESS)
    slurm_error("ADMIRE: Could not destroy IC context");

  return 0;
}
