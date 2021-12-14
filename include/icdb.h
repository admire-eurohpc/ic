#ifndef __ADMIRE_ICDB_H
#define __ADMIRE_ICDB_H

#include <uuid.h>               /* UUID_STR_LEN */
#include "icc_priv.h"           /* ICC_XX_LEN */


#define ICDB_FAILURE -1
#define ICDB_SUCCESS 0

#define ICDB_EDB       4        /* DB error */
#define ICDB_ENOMEM    5        /* out of memory */
#define ICDB_EPARAM    6        /* wrong parameter */
#define ICDB_EOTHER    2        /* misc errors */

#define ICDB_ERRSTR_LEN 256


struct icdb_context;


/**
 * Initialize connection the IC database.
 * Returns ICDB_SUCCESS or an error code.
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
 * Add an IC client identified by CLID to the database.
 */
int icdb_setclient(struct icdb_context *icdb, const char *clid,
                   const char *type, const char *addr, uint16_t provid, uint32_t jobid);

/**
 * IC client
 */
struct icdb_client {
  char clid[UUID_STR_LEN];
  char type[ICC_TYPE_LEN];
  char addr[ICC_ADDR_LEN];
  uint16_t provid;
  uint32_t jobid;
};

/**
 * Get the IC client CLID.
 */
int icdb_getclient(struct icdb_context *icdb, const char *clid, struct icdb_client *client);


#endif
