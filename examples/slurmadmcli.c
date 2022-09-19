#include <errno.h>
#include <inttypes.h>           /* PRId32 */
#include <stdint.h>             /* uint32_t, etc. */
#include <stdlib.h>             /* strtoul */
#include <slurm/spank.h>

#include "admire.h"

SPANK_PLUGIN (admire-cli, 1)

static int scord_flag = 0;  /* if there are scord hints */

/* scord options */
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
process_opts(int val __attribute__((unused)), const char *optarg, int remote __attribute__((unused)))
{
  if (spank_context() != S_CTX_LOCAL)
    return 0;

  /* if we're here some scord options were passed to the Slurm CLI */
  scord_flag = 1;

  /* XX check for overflow */
  unsigned long tmp;
  char *endptr;

  tmp = strtoul(optarg, &endptr, 0);
  if (errno != 0 || endptr == optarg || *endptr != '\0') {
        slurm_error("invalid argument");
  }

  if (tmp <= 0 || tmp > UINT32_MAX) {
    return -1;
  }
  adhoc_nnodes = (uint32_t)tmp;

  return 0;
}


/**
 * Called locally in srun, after jobid & stepid are available.
 */
int
slurm_spank_local_user_init(spank_t sp, int ac __attribute__((unused)),
                                        char **av __attribute__((unused)))
{
  if (!scord_flag)
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

  ADM_server_t scord_server;
  scord_server = ADM_server_create("ofi+tcp://127.0.0.1", "ofi+tcp://127.0.0.1:52000");
  if (!scord_server) {
    slurm_error("failed scord server creation");
  }

  ADM_adhoc_context_t adhoc_ctx;
  ADM_storage_t adhoc_storage;

  adhoc_ctx = ADM_adhoc_context_create(ADM_ADHOC_MODE_SEPARATE_NEW, ADM_ADHOC_ACCESS_RDWR,
                                       adhoc_nnodes, 100, false);

  adhoc_storage = ADM_storage_create("foo", ADM_STORAGE_GEKKOFS, adhoc_ctx);

  /* no inputs or outputs */
  ADM_job_requirements_t scord_reqs;
  scord_reqs = ADM_job_requirements_create(NULL, 0, NULL, 0, adhoc_storage);
  if (!scord_reqs) {
    slurm_error("scord: failed to create job requirements");
  }
  slurm_error("scord: ADM_job_requirements_create");

  ADM_job_t scord_job;
  ADM_register_job(scord_server, scord_reqs, &scord_job);

  ADM_storage_destroy(adhoc_storage);
  ADM_job_requirements_destroy(scord_reqs);
  ADM_server_destroy(scord_server);

  return 0;
}
