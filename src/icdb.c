#include <errno.h>
#include <inttypes.h>           /* PRIuXX */
#include <stdlib.h>             /* malloc */
#include <string.h>             /* strncpy */
#include <hiredis.h>
#include "icdb.h"


#define CHECK_ICDB(icdb) if (!(icdb)) { return ICDB_EPARAM; }

#define ICDB_SET_STATUS(icdb,rc,...)  _icdb_set_status(icdb, rc, __func__, __VA_ARGS__)

#define ICDB_GET_UINT32(icdb,rep,name)  (uint32_t)_icdb_get_uint(icdb, rep, name, UINT32_MAX)

#define ICDB_GET_STR(icdb,rep,dest,name,maxlen) if (rep->len <= maxlen) { \
    strcpy(dest, rep->str);                                               \
  } else {                                                                \
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Field \"%s\" is too large", name);\
  }


struct icdb_context {
  redisContext *redisctx;
  int           status;
  char          errstr[ICDB_ERRSTR_LEN];
};


/* internal utility functions */
static int
_icdb_set_status(struct icdb_context *icdb, int status, const char *funcname, const char *format, ...);
static unsigned long long
_icdb_get_uint(struct icdb_context *icdb, redisReply *rep, const char *fieldname, unsigned long long max);


/* public functions */
int
icdb_init(struct icdb_context **icdb_context) {
  *icdb_context = NULL;

  struct icdb_context *icdb = calloc(1, sizeof(struct icdb_context));
  if (!icdb)
    return ICDB_ENOMEM;

  icdb->status = ICDB_SUCCESS;

  icdb->redisctx = redisConnect("127.0.0.1", 6379);
  if (icdb->redisctx == NULL) {
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Null DB context");
  }

  if (icdb->redisctx->err) {
    icdb->status = ICDB_EDB;
  }

  *icdb_context = icdb;

  return icdb->status;
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
  if (!icdb)
    return NULL;

  if (icdb->status == ICDB_EDB && icdb->redisctx) {
    return icdb->redisctx->errstr;
  } else if (icdb->status != ICDB_SUCCESS) {
    return icdb->errstr;
  } else {
    return NULL;
  }
}


int
icdb_command(struct icdb_context *icdb, const char *format, ...) {
  CHECK_ICDB(icdb);

  if (!icdb->redisctx)
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Null database context");

  va_list ap;
  va_start(ap, format);
  redisReply *redisrep = redisvCommand(icdb->redisctx, format, ap);
  va_end(ap);

  if (redisrep == NULL)
    return ICDB_EDB;

  switch (redisrep->type) {
  case REDIS_REPLY_STATUS:
  case REDIS_REPLY_ERROR:
  case REDIS_REPLY_STRING:
    fprintf(stderr, "REDIS: %s\n", redisrep->str);
    break;
  case REDIS_REPLY_INTEGER:
    fprintf(stderr, "REDIS: %lld", redisrep->integer);
    break;
  case REDIS_REPLY_NIL:
    break;
  case REDIS_REPLY_ARRAY:
    fprintf(stderr, "REDIS: got %ld elements\n", redisrep->elements);
    for (size_t i = 0; i < redisrep->elements; i++) {
      fprintf(stderr, "REDIS: %s\n", redisrep->element[i]->str);
    }
    break;
  }
  /* XX handle other Redis response */

  freeReplyObject(redisrep);

  return ICDB_SUCCESS;
}


int
icdb_setclient(struct icdb_context *icdb, const char *clid,
               const char *type, const char *addr, uint16_t provid, uint32_t jobid)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  if (!icdb->redisctx || !clid || !type || !addr) {
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Missing parameter");
    return icdb->status;
  }

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  rep = redisCommand(ctx, "HSET client:%s clid %s type %s addr %s provid %"PRIu32" jobid %"PRIu32,
                     clid, clid, type, addr, provid, jobid);

  /* HSET returns the number of fields that were added */
  if (rep->type != REDIS_REPLY_INTEGER || rep->integer != 5) {
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Wrong number of fields written");
  }

  return icdb->status;
}


int
icdb_getclient(struct icdb_context *icdb, const char *clid, struct icdb_client *client)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  if (!icdb->redisctx || !clid || !client) {
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Missing parameter");
    return icdb->status;
  }

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  rep = redisCommand(ctx, "HGETALL client:%s", clid);

  /* HGETALL returns all keys followed by their respective value */
  for (size_t i = 0; i < rep->elements; i++) {
    char *key = rep->element[i]->str;
    redisReply *val = rep->element[++i];

    if (!strcmp(key, "clid")) {
      ICDB_GET_STR(icdb, val, client->clid, key, UUID_STR_LEN);
    }
    else if (!strcmp(key, "type")) {
      ICDB_GET_STR(icdb, val, client->type, key, ICC_TYPE_LEN);
    }
    else if (!strcmp(key, "addr")) {
      ICDB_GET_STR(icdb, val, client->addr, key, ICC_ADDR_LEN);
    }
    else if (!strcmp(key, "provid")) {
      client->provid = ICDB_GET_UINT32(icdb, val, key);
    }
    else if (!strcmp(key, "jobid")) {
      client->jobid = ICDB_GET_UINT32(icdb, val, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
  }

  return icdb->status;
}


int
icdb_delclient(struct icdb_context *icdb, const char *clid)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  if (!icdb->redisctx || !clid) {
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Missing parameter");
    return icdb->status;
  }

  redisReply *rep;
  rep = redisCommand(icdb->redisctx, "DEL client:%s", clid);

  /* DEL returns the number of keys that were deleted */
  if (rep->type != REDIS_REPLY_INTEGER || rep->integer != 1) {
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Wrong number of client deleted");
  }

  return icdb->status;
}


/* utils */
static int
_icdb_set_status(struct icdb_context *icdb, int status, const char *funcname, const char *format, ...)
{
  CHECK_ICDB(icdb);

  if (!format)
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Null format parameter");

  icdb->status = status;

  size_t len = ICDB_ERRSTR_LEN - 1;       /* -1 for terminating '\0'  */
  size_t funclen = 0;

  if (funcname) {
    funclen = strnlen(funcname, len) + 2; /* +2 for ": " */
  }
  if (len - funclen > 0) {
    sprintf(icdb->errstr, "%s: ", funcname);
  }

  va_list ap;
  va_start(ap, format);

  vsnprintf(icdb->errstr + funclen, len - funclen, format, ap);
  icdb->errstr[ICDB_ERRSTR_LEN - 1] = '\0';

  va_end(ap);

  return ICDB_SUCCESS;
}


static unsigned long long
_icdb_get_uint(struct icdb_context *icdb, redisReply *rep, const char *fieldname, unsigned long long max)
{
  if (!icdb || !rep || !fieldname) {
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Bad parameters");
    return 0;
  }
  icdb->status = ICDB_SUCCESS;

  unsigned long long res;
  char *end;

  errno = 0;
  res = strtoull(rep->str, &end, 0);
  if (errno != 0 || end == rep->str || *end != '\0') {
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Field \"%s\" is not an integer (%s)", fieldname, rep->str);
    return 0;
  }
  else if (res > max) {
    ICDB_SET_STATUS(icdb, ICDB_EOTHER, "Field \"%s\" is too large", fieldname);
    return max;
  }
  else {
    return res;
  }

  return icdb->status;
}
