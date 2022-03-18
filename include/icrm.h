#ifndef __ADMIRE_ICRM_H
#define __ADMIRE_ICRM_H

#include <stdint.h>

/**
 * Resouce manager (RM) related functions, for use by the IC server.
 */

enum icrm_error {
  ICRM_SUCCESS = 0,
  ICRM_FAILURE,
  ICRM_EPARAM,
  ICRM_ENOTIMPL,                /* not implemented */
  ICRM_ENOMEM,
  ICRM_EJOBID,
  ICRM_ERESOURCEMAN,            /* resource manager */
  ICRM_ECOUNT,
};
typedef enum icrm_error icrmerr_t;

/* taken from slurm.h */
enum icrm_jobstate {
  ICRM_JOB_PENDING,     /* queued waiting for initiation */
  ICRM_JOB_RUNNING,     /* allocated resources and executing */
  ICRM_JOB_OTHER
};

/**
 * Context passed to ICRM functions. NOT thread-safe.
 */
typedef struct icrm_context icrm_context_t;

/**
 * Initialize an ICRM context.
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_init(icrm_context_t **icrm);


/**
 * Finalize an ICRM context.
 */
void icrm_fini(icrm_context_t **icrm);


/**
 * Return the string describing the latest ICRM error or NULL if ICRM
 * is undefined. The content is only meaningful AFTER an ICRM function
 * has returned with an error code.
 */
char *icrm_errstr(icrm_context_t *icrm);


/**
 * Query the resource manager for the state of JOBID. The result is
 * returned in JOBSTATE.
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_jobstate(icrm_context_t *icrm, uint32_t jobid,
                        enum icrm_jobstate *jobstate);


/**
 * Request a new allocation of NNODES to the resource manager. If
 * SHRINK is true, give back that many nodes (NOT IMPLEMENTED). Blocks
 * until the allocation has been granted.
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_alloc(icrm_context_t *icrm, char shrink, uint32_t nnodes);

#endif
