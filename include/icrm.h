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
 * Initialize ICRM.
 */
void icrm_init(void);


/**
 * Finalize ICRM.
 */
void icrm_fini(void);


/**
 * Query the resource manager for the state of JOBID. The result is
 * returned in JOBSTATE.
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_jobstate(uint32_t jobid, enum icrm_jobstate *jobstate,
                        char errstr[ICC_ERRSTR_LEN]);

/**
 * Query the resource manager for the number of cpus and the number of
 * nodes assocoated with JOBID. The result is returned in NCPUS and
 * NNODES respectively.
 *
 * Return ICRM_SUCCESS or an error code. Fill errstr in case of error.
 */
icrmerr_t icrm_ncpus(uint32_t jobid, uint32_t *ncpus, uint32_t *nnodes,
                     char errstr[ICC_ERRSTR_LEN]);


/**
 * Request a new allocation of NCPUS to the resource manager. Blocks
 * until the allocation has been granted. This will generate a new job
 * NEWJOBID with the resources requested, call icrm_merge to merge
 * them in job JOBID.
 *
 * Return the new jobid in NEWJOBID, the actual number of CPUs granted
 * in NCPUS and a host(char *):ncpus(uint16_t) map in HOSTMAP.
 *
 * Return ICRM_SUCCESS or an error code. Fill errstr in case of error.
 *
 * The caller is responsible for freeing HOSTMAP.
 */
icrmerr_t icrm_alloc(uint32_t jobid, uint32_t *newjobid, uint32_t *ncpus,
                     hm_t **hostmap, char errstr[ICC_ERRSTR_LEN]);

/**
 * Renounce the resources of job JOBID and merge them with the job for
 * which they had been requested. This is equivalent to calling
 * "scontrol update JobId=$JOBID NumNodes=0"
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_merge(uint32_t jobid, char errstr[ICC_ERRSTR_LEN]);


/**
 * Release node NODENAME to the resource manager, after checking that
 * JOBID indeed used NCPUS from this node.
 *
 * Return ICRM_SUCCESS or an error code. If ICRM_EAGAIN is returned it
 * means that the node cannot be released because more CPUs have been
 * allocated on it.
 */
icrmerr_t icrm_release_node(const char *nodename, uint32_t jobid, uint32_t ncpus,
                            char errstr[ICC_ERRSTR_LEN]);


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
