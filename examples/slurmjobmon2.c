/* SLURMJOBMON2
 * Job monitoring plugin writing directly to the IC database
 */

#include <errno.h>
#include <inttypes.h>           /* PRId32 */
#include <stddef.h>             /* NULL */
#include <stdint.h>             /* uint32_t, etc. */
#include <stdlib.h>             /* getenv */
#include <string.h>             /* strerror */
#include <time.h>				/* time */
#include <slurm/spank.h>
#include <hiredis.h>


SPANK_PLUGIN(job-monitor, 1)

struct dbcontext {
  redisContext *redisctx;
};
struct dbcontext db;

uint32_t jobid, jobstepid;

/*
 * DB utils
 */
int
dbinit()
{
  db.redisctx = redisConnect("127.0.0.1", 6379);
  if (db.redisctx == NULL || db.redisctx->err) {
    return 1;
  }

  return 0;
}

void
dbfini()
{
  redisFree(db.redisctx);
}


/*
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


/*
 * Called locally in srun, after jobid & stepid are available.
 * Errors are not fatal (reading Slurm values, DB connection), because
 * we don't want to block job submission.
 */
int
slurm_spank_local_user_init(spank_t sp,
                            int ac __attribute__((unused)),
                            char **av __attribute__((unused)))
{
  if (!adm_enabled())
    return 0;

  spank_err_t sprc;
  uint32_t nnodes;

  sprc = spank_get_item(sp, S_JOB_ID, &jobid);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error("ADMIRE: slurmjobmon2: failed getting jobid: %s.", spank_strerror(sprc));
    goto end;
  }

  sprc = spank_get_item(sp, S_JOB_STEPID, &jobstepid);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error("ADMIRE: slurmjobmon2: failed getting jobstepid: %s.", spank_strerror(sprc));
    goto end;
  }

  sprc = spank_get_item(sp, S_JOB_NNODES, &nnodes);
  if (sprc != ESPANK_SUCCESS) {
    slurm_error("ADMIRE: slurmjobmon2: failed getting nnodes: %s.", spank_strerror(sprc));
    goto end;
  }

  time_t start = time(NULL);
  if (start == (time_t)-1) {
    slurm_error("ADMIRE: slurmjobmon2: time: %s", strerror(errno));
    goto end;
  }

  if (dbinit()) {
    slurm_error("ADMIRE: slurmjobmon2: db init failed");
    goto end;
  }

  redisReply *rep;
  rep = redisCommand(db.redisctx, "HSET job:%"PRIu32".%"PRIu32" nnodes %"PRIu32" start %"PRId64, jobid, jobstepid, nnodes, start);

  if (rep->type == REDIS_REPLY_ERROR) {
    slurm_error("ADMIRE: slurmjobmon2: db write: %s", rep->str);
  }

end:
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

  time_t end = time(NULL);
  if (end == (time_t)-1) {
    slurm_error("ADMIRE: slurmjobmon2: time: %s", strerror(errno));
    goto end;
  }

  redisReply *rep;
  rep = redisCommand(db.redisctx, "HSET job:%"PRIu32".%"PRIu32" end %"PRId64, jobid, jobstepid, end);

  if (rep->type == REDIS_REPLY_ERROR) {
    slurm_error("ADMIRE: slurmjobmon2: db write: %s", rep->str);
  }


end:
  dbfini();
  return 0;
}
