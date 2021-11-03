#ifndef __ADMIRE_ICDB_H
#define __ADMIRE_ICDB_H


#define ICDB_ERROR -1
#define ICDB_SUCCESS 0

#define ICDB_EPROTOCOL 4        /* protocol error */
#define ICDB_ENOMEM    5        /* out of memory */
#define ICDB_EPARAM    6        /* out of memory */
#define ICDB_EOTHER    2        /* misc errors */


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
 * The string will be overwritten in case of further errors, so itmust
 * be called immediately after the error.
 */
char *icdb_errstr(struct icdb_context *icdb);

/**
 * Pass COMMAND to the the IC database.
 * NB: At the moment, this is but a thin wrapper over hiredis API.
 */
int icdb_command(struct icdb_context *icdb, const char *format, ...);

#endif
