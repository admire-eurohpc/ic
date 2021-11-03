#include <stdlib.h>             /* malloc */
#include <hiredis.h>
#include "icdb.h"

struct icdb_context {
  redisContext *redisctx;
};


int
icdb_init(struct icdb_context **icdb_context) {
  *icdb_context = NULL;

  struct icdb_context *icdb = calloc(1, sizeof(struct icdb_context));
  if (!icdb)
    return ICDB_ENOMEM;

  icdb->redisctx = redisConnect("127.0.0.1", 6379);
  if (icdb->redisctx == NULL) {
    free(icdb);
    return ICDB_EOTHER;
  }

  *icdb_context = icdb;

  if (icdb->redisctx->err)
    return ICDB_EPROTOCOL;

  return ICDB_SUCCESS;
}


void
icdb_fini(struct icdb_context **icdb) {
  if (icdb) {
      if (*icdb)
        redisFree((*icdb)->redisctx);
      free(*icdb);
  }
  *icdb = NULL;
}


char *
icdb_errstr(struct icdb_context *icdb) {
  if (icdb && icdb->redisctx)
    return icdb->redisctx->errstr;
  else
    return NULL;
}


int
icdb_command(struct icdb_context *icdb, const char *format, ...) {
  if (!icdb || !icdb->redisctx)
    return ICDB_EPARAM;

  va_list ap;
  va_start(ap, format);
  redisReply *redisrep = redisvCommand(icdb->redisctx, format, ap);
  va_end(ap);

  if (redisrep == NULL)
    return ICDB_EPROTOCOL;
  /* XX handle other Redis response */

  freeReplyObject(redisrep);

  return ICDB_SUCCESS;
}
