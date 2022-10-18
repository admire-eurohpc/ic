#include <errno.h>
#include <stdint.h>				/* SIZE_MAX, uint32_t, etc. */
#include <stdlib.h>				/* strtoul, getenv, reallocarray */
#include <string.h>				/* strchr, strncmp, strncpy */
#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "admire.h"

/**
 * Slurm SPANK plugin to handle the ADMIRE adhoc storage CLI. Options are
 * forwarded to scord on srun, salloc and sbatch. See the struct spank_option
 * for the list of options.
 *
 * Notes:
 * - --adm-adhoc-context-id will be silently truncated to ADHOCID_LEN
 *   characters, including NULL byte
 **/

#define SCORD_SERVER_DEFAULT "ofi+tcp://127.0.0.1:52000"
#define SCORD_PROTO_DEFAULT  "ofi+tcp"
#define ADHOCID_LEN  64

#define TAG_NNODES     1
#define TAG_WALLTIME   2
#define TAG_MODE       3
#define TAG_CONTEXT_ID 4

SPANK_PLUGIN (admire-cli, 1)

static int scord_flag = 0;

/* scord adhoc options */
static long adhoc_nnodes = 0;
static long adhoc_walltime = 0;
static ADM_adhoc_mode_t adhoc_mode = 0;
static char adhoc_id[ADHOCID_LEN] = { 0 };

static int process_opts(int val, const char *optarg, int remote);

struct spank_option spank_opts [] = {
	{
		"adm-adhoc-nodes",
		"[nnodes]",
		"Dedicate [nnodes] to the ad-hoc storage",
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
		long tmp;
		char *endptr;
		errno = 0;

		tmp = strtol(optarg, &endptr, 0);
		if (errno != 0 || endptr == optarg || *endptr != '\0' || tmp <= 0) {
			return -1;
		}

		if (tag == TAG_NNODES) {
			adhoc_nnodes = tmp;
		}
		if (tag == TAG_WALLTIME) {
			adhoc_walltime = tmp;
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
		strncpy(adhoc_id, optarg, ADHOCID_LEN - 1);
		adhoc_id[ADHOCID_LEN - 1] = '\0';
	}

	return 0;
}

static int
scord_register_job(const char *scord_proto, const char *scord_addr, const char *nodelist, uint32_t jobid)
{
	int rc = 0;

	ADM_server_t scord_server;
	scord_server = ADM_server_create(scord_proto, scord_addr);
	if (!scord_server) {
		slurm_error("slurmadmcli: scord server creation failed");
		rc = -1;
		goto end;
	}

	/* list of job nodes */
	hostlist_t hl = slurm_hostlist_create(nodelist);
	if (!hl) {
		slurm_error("slurmadmcli: slurm_hostlist creation failed");
		rc = -1;
		goto end;
	}

	int nnodes = slurm_hostlist_count(hl);
	if (nnodes <= 0) {
		slurm_error("slurmadmcli: wrong slurm_hostlist count");
		rc = -1;
		goto end;
	}

	ADM_node_t *nodes = reallocarray(NULL, nnodes, sizeof(ADM_node_t));
	if (!nodes) {
		slurm_error("slurmadmcli: out of memory");
		rc = -1;
		goto end;
	}

	size_t i = 0;
	char *nodename;
	while((nodename = slurm_hostlist_shift(hl))) {
		nodes[i] = ADM_node_create(nodename);
		if (!nodes[i]) {
			slurm_error("slurmadmcli: scord node creation failed");
			rc = -1;
			goto end;
		}
		i++;
	}

	ADM_job_resources_t job_resources;
	job_resources = ADM_job_resources_create(nodes, nnodes);
	if (!job_resources) {
		slurm_error("slurmadmcli: job_resources creation failed");
		rc = -1;
		goto end;
	}

	/* take the ADHOC_NNODES first nodes for the adhoc */
	ADM_adhoc_resources_t adhoc_resources;
	adhoc_resources = ADM_adhoc_resources_create(nodes, adhoc_nnodes < nnodes ? adhoc_nnodes : nnodes);
	if (!adhoc_resources) {
		slurm_error("slurmadmcli: adhoc_resources creation failed");
		rc = -1;
		goto end;
	}

	ADM_adhoc_context_t adhoc_ctx;
	adhoc_ctx = ADM_adhoc_context_create(adhoc_mode,ADM_ADHOC_ACCESS_RDWR,
										 adhoc_resources, adhoc_walltime, false);
	if (!adhoc_ctx) {
		slurm_error("slurmadmcli: adhoc_context creation failed");
		rc = -1;
		goto end;
	}

	ADM_storage_t adhoc_storage;
	if (ADM_register_adhoc_storage(scord_server, "mystorage", ADM_STORAGE_GEKKOFS, adhoc_ctx, &adhoc_storage) != ADM_SUCCESS) {
		slurm_error("slurmadmcli: adhoc_storage registration failed");
		rc = -1;
		goto end;
	}

	/* no inputs or outputs */
	ADM_job_requirements_t scord_reqs;
	scord_reqs = ADM_job_requirements_create(NULL, 0, NULL, 0, adhoc_storage);
	if (!scord_reqs) {
		slurm_error("slurmadmcli: scord job_requirements creation");
		rc = -1;
		goto end;
	}

	ADM_job_t scord_job;
	if (ADM_register_job(scord_server, job_resources, scord_reqs, jobid, &scord_job) != ADM_SUCCESS) {
		slurm_error("slurmadmcli: scord job registration failed");
		rc = -1;
		goto end;
	}

	if (ADM_deploy_adhoc_storage(scord_server, adhoc_storage) != ADM_SUCCESS) {
		slurm_error("slurmadmcli: adhoc storage deployment failed");
		rc = -1;
		goto end;
	}

end:
	slurm_hostlist_destroy(hl);
	ADM_adhoc_resources_destroy(adhoc_resources);
	ADM_remove_job(scord_server, scord_job);
	ADM_job_requirements_destroy(scord_reqs);
	ADM_storage_destroy(adhoc_storage);
	ADM_server_destroy(scord_server);
	return rc;
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


int
slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
	(void)sp;

	if (!scord_flag)
		return 0;

	const char *scord_addr = SCORD_SERVER_DEFAULT;
	const char *scord_proto = SCORD_PROTO_DEFAULT;

	for (int i = 0; i < ac; i++) {
		if (!strncmp ("scord_addr=", av[i], 11)) {
			scord_addr = av[i] + 11;
		} else if (!strncmp ("scord_proto=", av[i], 12)) {
			scord_proto = av[i] + 12;
		} else {
			slurm_error("slurmadmcli: invalid option: %s", av[i]);
			return -1;
		}
	}

	/* get job id */
	uint32_t jobid;
	spank_err_t rc;
	if ((rc = spank_get_item(sp, S_JOB_ID, &jobid)) != ESPANK_SUCCESS) {
		slurm_error ("slurmadmcli: failed to get jobid: %s", spank_strerror(rc));
		return -1;
	}

	/* get list of nodes. /!\ at this point env SLURM_NODELIST is
	   set, but not SLURM_JOB_NODELIST! */
	const char *nodelist = getenv("SLURM_NODELIST");
	if (!nodelist) {
		slurm_error("slurmadmcli: failed to get node list");
		return -1;

	return scord_register_job(scord_proto, scord_addr, nodelist, jobid);
}
