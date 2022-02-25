#include <assert.h>
#include <stdlib.h>             /* calloc */
#include <string.h>             /* strncpy */
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "icc_priv.h"
#include "icrm.h"

#define CHECK_NULL(p)  if (!(p)) { return ICRM_EPARAM; }
#define CHECK_ICRM(icrm)  { CHECK_NULL(icrm); CHECK_NULL((icrm)->errstr); }


static enum icrm_jobstate _slurm2icrmstate(enum job_states slurm_state);
static icrmerr_t _writerr(icrm_context_t *icrm, char *errstr);

struct icrm_context {
  char errstr[ICC_ERRSTR_LEN];
};


icrmerr_t
icrm_init(icrm_context_t **icrm)
{
  CHECK_NULL(icrm);

  *icrm = NULL;

  icrm_context_t *new = calloc(1, sizeof(*new));
  if (!new)
    return ICRM_ENOMEM;

  *icrm = new;

  return ICRM_SUCCESS;
}


void
icrm_fini(icrm_context_t **icrm)
{
  if (icrm) {
    if (*icrm) {
      free(*icrm);
    }
    *icrm = NULL;
  }
}


char *
icrm_errstr(icrm_context_t *icrm)
{
  if (!(icrm && icrm->errstr))
    return NULL;

  return icrm->errstr;
}


icrmerr_t
icrm_jobstate(icrm_context_t *icrm, uint32_t jobid, enum icrm_jobstate *jobstate)
{
  CHECK_ICRM(icrm);
  CHECK_NULL(jobstate);

  icrmerr_t rc;
  int slurmrc;
  job_info_msg_t *buf = NULL;

  rc = ICRM_SUCCESS;

  slurmrc = slurm_load_job(&buf, jobid, SHOW_ALL);

  if (slurmrc != SLURM_SUCCESS) {
    if (errno == ESLURM_INVALID_JOB_ID) {
      rc = ICRM_EJOBID;
    } else{
      rc = ICRM_FAILURE;
    }
    _writerr(icrm, slurm_strerror(errno));
    goto end;
  }

  /* only one job with a given jobid? */
  assert(buf->record_count == 1);

  *jobstate = _slurm2icrmstate(buf->job_array[0].job_state);

 end:
  if (buf) {
    slurm_free_job_info_msg(buf);
  }
  return rc;
}


static icrmerr_t
_writerr(icrm_context_t *icrm, char *errstr)
{
  CHECK_ICRM(icrm);
  CHECK_NULL(errstr);

  strncpy(icrm->errstr, errstr, ICC_ERRSTR_LEN);
  icrm->errstr[ICC_ERRSTR_LEN - 1] = '\0';

  return ICRM_SUCCESS;
}


static enum icrm_jobstate
_slurm2icrmstate(enum job_states slurm_jobstate)
{
  switch (slurm_jobstate) {
  case JOB_PENDING:
    return ICRM_JOB_PENDING;
  case JOB_RUNNING:
    return ICRM_JOB_RUNNING;
  case JOB_SUSPENDED:
  case JOB_COMPLETE:
  case JOB_CANCELLED:
  case JOB_FAILED:
  case JOB_TIMEOUT:
  case JOB_NODE_FAIL:
  case JOB_PREEMPTED:
  case JOB_BOOT_FAIL:
  case JOB_DEADLINE:
  case JOB_OOM:
  case JOB_END:
    break;
  }
  return ICRM_JOB_OTHER;
}
