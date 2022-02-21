#ifndef __ADMIRE_ICDB_H
#define __ADMIRE_ICDB_H

#include "uuid_admire.h"        /* UUID_STR_LEN */
#include "icc_priv.h"           /* ICC_xx_LEN */


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
int icdb_init(struct icdb_context **icdb);

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
# define ICDB_CLIENT_NFIELDS 6

struct icdb_client {
  char clid[UUID_STR_LEN];
  char type[ICC_TYPE_LEN];
  char addr[ICC_ADDR_LEN];
  uint16_t provid;
  uint32_t jobid;
  uint64_t nprocs;              /* nprocesses in client */
};

inline void
icdb_initclient(struct icdb_client *client) {
  if (!client)
    return;

  client->clid[0] = '\0';
  client->type[0] = '\0';
  client->addr[0] = '\0';
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
  uint32_t ntasks;
};

inline void
icdb_initjob(struct icdb_job *job) {
  if (!job) return;
  job->jobid = 0;
  job->nnodes = 0;
  job->ntasks= 0;
}


/**
 * Add an IC client identified by CLID to the database.
 */
int icdb_setclient(struct icdb_context *icdb, const char *clid,
                   const char *type, const char *addr, uint16_t provid,
                   uint32_t jobid, uint32_t jobntasks, uint32_t jobnnodes, uint64_t nprocs);

/**
 * Get the IC client CLID.
 *
 * Returns ICDB_SUCCESS or an error code in case of error. In
 * particular, ICDB_NORESULT is returned if there is no client with
 * this ID.
 */
int icdb_getclient(struct icdb_context *icdb, const char *clid, struct icdb_client *client);


/**
 * Get no more than COUNT IC clients into the array CLIENTS of size
 * SIZE. Filter by TYPE or JOBID. If TYPE is NULL or JOBID is 0, the
 * corresponding filter is not applied.
 *
 * The number of clients found is returned in COUNT. ICDB_E2BIG is
 * returned if CLIENTS is too small to store them all. In this case,
 * the caller has to resize the array accordingly. Note that because
 * the filtering XX
 *
 * This is a cursor based iterator: call with CURSOR = 0 the first
 * time, then pass the cursor back to the next calls, until it comes
 * back 0, at which point all clients have been returned from
 * database.
 */
int icdb_getclients(struct icdb_context *icdb, const char *type, uint32_t jobid,
                    struct icdb_client clients[], size_t size, size_t *count);


/**
 * Delete IC client CLID.
 *
 * Returns the corresponding jobid in JOBID.
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_delclient(struct icdb_context *icdb, const char *clid, uint32_t *jobid);


/**
 * Increment the process count of client CLID by INCRBY. INCRBY can be
 * negative, in which case the process count is decremented.
 *
 * Returns ICDB_SUCCESS or an error code.
 */
int icdb_incrnprocs(struct icdb_context *icdb, char *clid, int64_t incrby);


/**
 * Get job JOBID into JOB. Allocation of job is the responsibility of
 * the caller.
 *
 * Returns ICDB_SUCCESS or an error code.
 */

int icdb_getjob(struct icdb_context *icdb, uint32_t jobid, struct icdb_job *job);


#endif
