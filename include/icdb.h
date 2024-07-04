#ifndef __ADMIRE_ICDB_H
#define __ADMIRE_ICDB_H

#include "uuid_admire.h"        /* UUID_STR_LEN */
#include "icc_common.h"         /* ICC_xx_LEN */


#define ICDB_SUCCESS  0
#define ICDB_FAILURE  1         /* generic error */
#define ICDB_EPROTO   2         /* db protocol error */
#define ICDB_E2BIG    3         /* buffer too small */
#define ICDB_EPARAM   4         /* wrong parameter */
#define ICDB_EBADRESP 5         /* bad DB response */
#define ICDB_ENOMEM   6         /* out of memory */
#define ICDB_NORESULT 7         /* no result to query */

#define ICDB_ERRSTR_LEN 256


struct icdb_context;


/**
 * Initialize connection the IC database.
 * Returns ICDB_SUCCESS or an error code.
 *
 * Warning: An icdb context is NOT thread-safe.
 */
int icdb_init(struct icdb_context **icdb, char *ip_addr);

/**
 * Finalize the connection to the IC database.
 */
void icdb_fini(struct icdb_context **icdb);

/**
 * In case of error, return an error string suitable for display.
 * The string will be overwritten in case of further errors, so it
 * must be called immediately after the error.
 */
char *icdb_errstr(struct icdb_context *icdb);

/**
 * Pass COMMAND to the the IC database.
 * NB: At the moment, this is but a thin wrapper over hiredis API.
 */
int icdb_command(struct icdb_context *icdb, const char *format, ...);

/**
 * IC client
 * XX fixme: get rid of NFIELDS
 */
#define ICDB_NODELIST_LEN   512
#define ICDB_CLIENT_NFIELDS 9

struct icdb_client {
  char clid[UUID_STR_LEN];
  char type[ICC_TYPE_LEN];
  char addr[ICC_ADDR_LEN];
  char nodelist[ICDB_NODELIST_LEN];
  uint16_t provid;
  uint32_t jobid;
  uint64_t nprocs;              /* nprocesses in client */
  int32_t reconfig_nprocs;  /* procs requested by client for malleab. */
  int32_t reconfig_nnodes;
};

inline void
icdb_initclient(struct icdb_client *client) {
  if (!client)
    return;

  client->clid[0] = '\0';
  client->type[0] = '\0';
  client->addr[0] = '\0';
  client->nodelist[0] = '\0';
  client->provid = 0;
  client->jobid = 0;
  client->nprocs = 0;
}


/**
 * Job
 */

struct icdb_job {
  uint32_t jobid;
  uint32_t nnodes;
  uint32_t ncpus;
  char     *nodelist;  /* need to be malloced */
};

/**
 * Add new nodes to an IC client identified by CLID to the database.
 *
 ** Returns ICDB_SUCCESS or an error code in case of error.
 */
int icdb_addnodes(struct icdb_context *icdb, const char *clid, const char *nodelist);

/**
 * Delete new nodes to an IC client identified by CLID to the database.
 *
 ** Returns ICDB_SUCCESS or an error code in case of error.
 */
int icdb_delnodes(struct icdb_context *icdb, const char *clid, const char *nodelist);

/**
 * Get average monitor values from an CLID application.
 *
 ** Returns ICDB_SUCCESS or an error code in case of error.
 */
int icdb_getMonitor(struct icdb_context *icdb, const char *clid, double *rate_cpu, double *rate_mem, int *num_proc, double *rtime, double *ptime, double *ctime);
/**
 * Add an IC client identified by CLID to the database.
 */
int icdb_setclient(struct icdb_context *icdb, const char *clid,
                   const char *type, const char *addr, const char *nodelist,
                   uint16_t provid, uint32_t jobid, uint32_t jobncpus,
                   const char *jobnodelist, uint64_t nprocs);

/**
 * Get the IC client CLID.
 *
 * Returns ICDB_SUCCESS or an error code in case of error. In
 * particular, ICDB_NORESULT is returned if there is no client with
 * this ID.
 */
int icdb_getclient(struct icdb_context *icdb, const char *clid, struct icdb_client *client);

/**
 * Get the IC client using the highest amount of nodes.
 *
 * Returns ICDB_SUCCESS or an error code in case of error.
 */
int icdb_getlargestclient(struct icdb_context *icdb, struct icdb_client *client);

/**
 * Get no more than IC clients into the array of size COUNT. Filter by
 * JOBID if different from 0.
 *
 * COUNT is updated with the number of clients found. ICDB_E2BIG is
 * returned if CLIENTS is too small to store them all. In this case,
 * the caller has to resize the array accordingly.
 */
int icdb_getclients(struct icdb_context *icdb, uint32_t jobid,
                    struct icdb_client clients[], size_t *count);

/**
 * Get clients matching jobid and type, with 0 and "" respectively meaning any
 * XX intersection not implemented
 * return results in clients, the caller is responsible for freeing it.
 * Cursor based, start call with 0, iteration is finished when cursor is 0 again.
 * Note: we rely on redis returning a "reasonable" number of clients at each
 * call.
 *
 * Similar to icdb_getclients but cursor based.
 */

int
icdb_getclients2(struct icdb_context *icdb, uint32_t jobid, const char *type,
   struct icdb_client *clients[], size_t *count, uint64_t *cursor);

/**
 * Delete IC client CLID.
 *
 * Returns the corresponding jobid in JOBID.
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_delclient(struct icdb_context *icdb, const char *clid, uint32_t *jobid);

/**
 * Compute a shrinked node list for the client identified by clid.
 * Return the reduced node list in newnodelist, which the caller is
 * responsible for freeing.
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_shrink(struct icdb_context *icdb, char *clid, char **newnodelist);

/**
 * Increment the process count of client CLID by INCRBY. INCRBY can be
 * negative, in which case the process count is decremented.
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_incrnprocs(struct icdb_context *icdb, char *clid, int64_t incrby);

/**
 * Mark client CLID as reconfigurable and registers its hints.
 */
int icdb_reconfigurable(struct icdb_context *icdb, const char *clid, int32_t procs_hint, int32_t nodes_hint);

/**
 * Init an ICDB job.
 */
void icdb_job_init(struct icdb_job *job);

/**
 * Free an ICDB job, including allocated memory.
 */
void icdb_job_free(struct icdb_job **job);

/**
 * Get job JOBID into JOB. The job struct should be initialized with
 * icdb_job_init() and freed with icdb_job_free()
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_getjob(struct icdb_context *icdb, uint32_t jobid, struct icdb_job *job);


/**
 * Delete JOBID and all clients associated with it.
 *
 * XX No check performed, a running job could be deleted. Ask Slurm?
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_deljob(struct icdb_context *icdb, uint32_t jobid);


/**
 * Get the running job ID with the largest amount of nodes.
 * If no job is registered, the job ID is set to 0
 *
 * XX This job interface is incompatible with icdb_getjob/deljob
 *
 * Return ICDB_SUCCESS or an error code
 */
int icdb_getlargestjob(struct icdb_context *icdb, uint32_t *jobid);


/**
 * Get message from stream STREAMKEY
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_mstream_read(struct icdb_context *icdb, char *streamkey);

/*
 * Message streams
 */
struct icdb_beegfs {
  uint64_t  timestamp;
  uint32_t qlen;
};

int icdb_mstream_beegfs(struct icdb_context *icdb, struct icdb_beegfs *result);

#endif
