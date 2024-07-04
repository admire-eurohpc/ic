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
 * Query the resource manager for informatin associated with JOBID.
 * Nodelist is allocated by the function and must be freed by the
 * caller.
 * Return ICRM_SUCCESS or an error code. Fill errstr in case of error.
 */
icrmerr_t icrm_info(uint32_t jobid, uint32_t *ncpus, uint32_t *nnodes,
                    char **nodelist, char errstr[ICC_ERRSTR_LEN]);


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
// CHANGE JAVI
//icrmerr_t icrm_alloc(uint32_t jobid, uint32_t *newjobid, uint32_t *ncpus,
icrmerr_t icrm_alloc(uint32_t *newjobid, uint32_t *ncpus, uint32_t *nnodes, hm_t **hostmap, char errstr[ICC_ERRSTR_LEN]);
// END CHANGE JAVI

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

// CHANGE:  JAVI
/**
 * Get Hostmap from a currently running Slurm job using its JOBID.
 *
 * Return ICRM_SUCCESS or an error code. 
 */
icrmerr_t icrm_get_job_hostmap(uint32_t jobid, hm_t **hostmap,
                               char errstr[ICC_ERRSTR_LEN]);
// END CHANGE:  JAVI

/**
 * Augment existing HOSTMAP with the resources in NEWALLOC.
 *
 * Return ICRM_SUCCESS or ICRM_EOVERFLOW if the resulting number of
 * CPUs would be too big.
 */
icrmerr_t icrm_update_hostmap(hm_t *hostmap, hm_t *newalloc);

// CHANGE JAVI
/**
 * Augment existing HOSTMAP_JOB with the nodes in NEWALLOC to the job JOBID .
 *
 * Return ICRM_SUCCESS or ICRM_EOVERFLOW if the resulting number of
 * CPUs would be too big.
 */
icrmerr_t icrm_update_hostmap_job(hm_t *hostmap_job, hm_t *newalloc, uint32_t jobid);
// END CHANGE JAVI


/**
 * Return a comma-separated list of hostname from hashmap HOSTMAP. If
 * WITHCPUS is true, add the number of CPUs associated with the node:
 * host:ncpus. If NCPUS_TOTAL is not null, set it to the total number
 * of CPUs in HOSTMAP.
 *
 * The values of the hashmap must be pointers to uint16_t.
 *
 * Return the hostlist, NULL in case of a memory error.
 *
 * If the NCPUS_TOTAL does not fit the total number of CPUs, return
 * NULL and set NCPUS_TOTAL to UINT32_MAX.
 *
 * The caller is responsible for freeing the hostlist.
 */
char *icrm_hostlist(hm_t *hostmap, char withcpus, uint32_t *ncpus_total);

// CHANGE: JAVI
/**
 * Clear the  pending status of the job_id when the job is done allocating
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_clear_pending_job();

/**
 * Kill the pending job if there is one and wait until is signaled as empty
 *
 * Return ICRM_SUCCESS or an error code.
 */
icrmerr_t icrm_kill_wait_pending_job(char errstr[ICC_ERRSTR_LEN]);

#endif
