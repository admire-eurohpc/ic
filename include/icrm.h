#ifndef ADMIRE_ICRM_H
#define ADMIRE_ICRM_H

#include <stdint.h>
#include "hashmap.h"

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
  ICRM_EOVERFLOW,
  ICRM_EAGAIN,
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
 * Query the resource manager for the number of cpus and the number of
 * nodes assocoated with JOBID. The result is returned in NCPUS and
 * NNODES respectively.
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_ncpus(icrm_context_t *icrm, uint32_t jobid,
                     uint32_t *ncpus, uint32_t *nnodes);

/**
 * Request a new allocation of NCPUS to the resource manager. Blocks
 * until the allocation has been granted. This will generate a new job
 * NEWJOBID with the resources requested, call icrm_merge to merge
 * them in job JOBID.
 *
 * Return the new jobid in NEWJOBID, the actual number of CPUs granted
 * in NCPUS and a host(char *):ncpus(uint16_t) map in HOSTMAP.
 *
 * Return ICRM_SUCCESS or an error code.
 *
 * The caller is responsible for freeing HOSTMAP.
 */
icrmerr_t icrm_alloc(icrm_context_t *icrm, uint32_t jobid,
                     uint32_t *newjobid, uint32_t *ncpus, hm_t **hostmap);

/**
 * Renounce the resources of job JOBID and merge them with the job for
 * which they had been requested. This is equivalent to calling
 * "scontrol update JobId=$JOBID NumNodes=0"
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_merge(icrm_context_t *icrm, uint32_t jobid);


/**
 * Release node NODENAME to the resource manager, after checking that
 * JOBID indeed used NCPUS from this node.
 *
 * Return ICRM_SUCCESS or an error code. If ICRM_EAGAIN is returned it
 * means that the node cannot be released because more CPUs have been
 * allocated on it.
 */
icrmerr_t icrm_release_node(icrm_context_t *icrm, const char *nodename,
                            uint32_t jobid, uint32_t ncpus);


/**
 * Augment existing HOSTMAP with the resources in NEWALLOC.
 *
 * Return ICRM_SUCCESS or ICRM_EOVERFLOW if the resulting number of
 * CPUs would be too big.
 */
icrmerr_t icrm_update_hostmap(hm_t *hostmap, hm_t *newalloc);


/**
 * Return a comma-separated list of hostname from hashmap HOSTMAP. If
 * WITHCPUS is true, add the number of CPUs associated with the node:
 * host:ncpus.  The values of the hashmap must be pointers to
 * uint16_t.
 *
 * Return the hostlist or NULL in case of a memory error.
 *
 * The caller is responsible for freeing the hostlist.
 */
char *icrm_hostlist(hm_t *hostmap, char withcpus);

#endif
