#include <errno.h>
#include <inttypes.h>			/* PRId32 */
#include <stdint.h>				/* uint32_t, etc. */
#include <stdlib.h>				/* strtoul */
#include <string.h>				/* strchr, strncmp, strncpy */
#include <slurm/spank.h>

#include "admire.h"

/**
 * Slurm SPANK plugin to handle the ADMIRE adhoc storage CLI. Options are
 * forwarded to scord on running srun. See the struct spank_option for the
 * list of options.
 *
 * Notes:
 * - --adm-adhoc-context could be renamed to adm-adhoc-mode
 * - --adm-adhoc-context-id will be truncated to ADHOCID_LEN characters
 *   including NULL byte
 * - add a default wall time equal to the job's?
 * - missing options adhoc_access_type and adhoc_should_flush,
 *   adhoc_storage_type, adm_input/output
 * - the ADM_job_t handle is not used, need a version that does not set it?
 * - should the storage ID be an arbitrary string? jobid.jobstepid?
 * - RPCs to scord should have a timeout to avoid blocking srun/salloc
 *
 **/

#define ADHOCID_LEN  128

#define TAG_NNODES     1
#define TAG_WALLTIME   2
#define TAG_MODE       3
#define TAG_CONTEXT_ID 4

SPANK_PLUGIN (admire-cli, 1)

static int scord_flag = 0;

/* scord adhoc options */
static uint32_t adhoc_nnodes = 0;
static uint32_t adhoc_walltime = 0;
static ADM_adhoc_mode_t adhoc_mode = 0;
static char adhoc_context_id[ADHOCID_LEN] = { 0 };

static int process_opts(int val, const char *optarg, int remote);

struct spank_option spank_opts [] = {
	{
		"adm-adhoc-nodes",
		"[nbnodes]",
		"Dedicate [nbnodes] to the ad-hoc storage",
		1,                           /* option takes an argument */
		TAG_NNODES,                  /* option tag */
		(spank_opt_cb_f)process_opts /* callback  */
	},
	{
		"adm-adhoc-walltime",
		"[walltime]",
		"Reserve the ad-hoc storage for [walltime] seconds",
		1,
		TAG_WALLTIME,
		(spank_opt_cb_f)process_opts
	},
	{
		"adm-adhoc-context",
		"[context]",
		"Mode of operation for the ad-hoc storage: in_job:shared|dedicated | separate:new|existing",
		1,
		TAG_MODE,
		(spank_opt_cb_f)process_opts
	},
	{
		"adm-adhoc-context-id",
		"[context_id]",
		"Context ID of the ad-hoc storage",
		1,
		TAG_CONTEXT_ID,
		(spank_opt_cb_f)process_opts
	},
	SPANK_OPTIONS_TABLE_END
};

static int
process_opts(int tag, const char *optarg, int remote)
{
	(void)remote;

	/* srun & sbatch/salloc */
	spank_context_t sctx = spank_context();
	if (sctx != S_CTX_LOCAL && sctx != S_CTX_ALLOCATOR)
		return 0;

	/* if we're here some scord options were passed to the Slurm CLI */
	scord_flag = 1;

	if (tag == TAG_NNODES || tag == TAG_WALLTIME) {
		unsigned long tmp;
		char *endptr;
		tmp = strtoul(optarg, &endptr, 0);

		if (tag == TAG_NNODES) {
			adhoc_nnodes = (uint32_t)tmp;
		}
		if (tag == TAG_WALLTIME) {
			adhoc_walltime = (uint32_t)tmp;
		}

		if (errno != 0 || endptr == optarg || *endptr != '\0' ||
			tmp <= 0 || tmp > UINT32_MAX) {
			return -1;
		}
		return 0;
	}

	if (tag == TAG_MODE) {
		char *col = strchr(optarg, ':');
		int parsed = 0;

		if (col) {
			if (!strncmp(optarg, "in_job", 6)) {
				if (!strncmp(col + 1, "shared", 6)) {
					adhoc_mode = ADM_ADHOC_MODE_IN_JOB_SHARED;
					parsed = 1;
				}
				if (!strncmp(col + 1, "dedicated", 9)) {
					adhoc_mode = ADM_ADHOC_MODE_IN_JOB_DEDICATED;
					parsed = 1;
				}
			} else if (!strncmp(optarg, "separate", 8)) {
				if (!strncmp(col + 1, "new", 3)) {
					adhoc_mode = ADM_ADHOC_MODE_SEPARATE_NEW;
					parsed = 1;
				}
				if (!strncmp(col + 1, "existing", 8)) {
					adhoc_mode = ADM_ADHOC_MODE_SEPARATE_EXISTING;
					parsed = 1;
				}
			}
		}

		if (!parsed) {
			return -1;
		}
	}

	if (tag == TAG_CONTEXT_ID) {
		strncpy(adhoc_context_id, optarg, ADHOCID_LEN - 1);
		adhoc_context_id[ADHOCID_LEN - 1] = '\0';
	}

	return 0;
}

int
slurm_spank_init(spank_t sp, int ac, char **av)
{
	(void)ac;
	(void)av;

	spank_err_t rc = ESPANK_SUCCESS;

	spank_context_t sctx = spank_context();
	if (sctx == S_CTX_LOCAL || sctx == S_CTX_ALLOCATOR) {
		/* register adm/scord options */
		struct spank_option *opt = &spank_opts[0];
		while(opt->name) {
			rc = spank_option_register(sp, opt++);
		}
	}

	return rc == ESPANK_SUCCESS ? 0 : -1;
}

/**
 * Called locally in srun, after jobid & stepid are available.
 **/
int
slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{
	(void)sp;
	(void)ac;
	(void)av;
	int rc = 0;

	if (!scord_flag)
		return 0;

	ADM_server_t scord_server;
	scord_server = ADM_server_create("ofi+tcp://127.0.0.1", "ofi+tcp://127.0.0.1:52000");
	if (!scord_server) {
		slurm_error("failed scord server creation");
		rc = -1;
		goto end;
	}

	ADM_adhoc_context_t adhoc_ctx;
	ADM_storage_t adhoc_storage;

	adhoc_ctx = ADM_adhoc_context_create(adhoc_mode,ADM_ADHOC_ACCESS_RDWR,
										 adhoc_nnodes, adhoc_walltime, false);
	if (!adhoc_ctx) {
		slurm_error("failed adhoc context creation");
		rc = -1;
		goto end;
	}

	adhoc_storage = ADM_storage_create(adhoc_context_id, ADM_STORAGE_GEKKOFS, adhoc_ctx);
	if (!adhoc_storage) {
		slurm_error("failed adhoc storage options setting");
		rc = -1;
		goto end;
	}

	/* no inputs or outputs */
	ADM_job_requirements_t scord_reqs;
	scord_reqs = ADM_job_requirements_create(NULL, 0, NULL, 0, adhoc_storage);
	if (!scord_reqs) {
		slurm_error("failed scord job requirements creation");
		rc = -1;
		goto end;
	}

	ADM_job_t scord_job;
	if (ADM_register_job(scord_server, scord_reqs, &scord_job) != ADM_SUCCESS) {
		slurm_error("failed to register scord job");
		rc = -1;
		goto end;
	}

	end:
	ADM_remove_job(scord_server, scord_job);
	ADM_job_requirements_destroy(scord_reqs);
	ADM_storage_destroy(adhoc_storage);
	ADM_server_destroy(scord_server);

	return rc;
}
