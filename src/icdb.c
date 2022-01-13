#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIuXX */
#include <stdlib.h>             /* malloc */
#include <string.h>             /* strncpy */
#include <uuid.h>               /* UUID_STR_LEN */
#include <hiredis.h>
#include "icdb.h"

/** XX TODO
 *
 * Return generic failure or specific code?
 * Transaction around creation/deletion of clients
 * Implement type & jobid filters
 */

#define ICDB_SET_STATUS(icdb,rc,...)  _icdb_set_status(icdb, rc, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define ICDB_GET_STR(icdb,rep,dest,name,maxlen) switch (_icdb_get_str(rep, dest, maxlen)) { \
  case ICDB_SUCCESS:                                                    \
    ICDB_SET_STATUS(icdb, ICDB_SUCCESS, "Success");                     \
    break;                                                              \
  case ICDB_EPARAM:                                                     \
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Bad parameters for %s", name);  \
    break;                                                              \
  case ICDB_E2BIG:                                                      \
    ICDB_SET_STATUS(icdb, ICDB_E2BIG, "Field %s too long", name);       \
    break;                                                              \
  default:                                                              \
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Failure");                     \
  }

/* it is "safe" to cast to ull here, because _icdb_get_uint validates the range */
#define ICDB_GET_UINT(icdb,rep,dest,name,max)   switch (_icdb_get_uint(rep, (unsigned long long *)dest, max)) { \
  case ICDB_SUCCESS:                                                    \
    ICDB_SET_STATUS(icdb, ICDB_SUCCESS, "Success");                     \
    break;                                                              \
  case ICDB_EPARAM:                                                     \
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Bad parameters for %s", name);  \
    break;                                                              \
  case ICDB_E2BIG:                                                      \
    ICDB_SET_STATUS(icdb, ICDB_E2BIG, "Field %s larger than "#max, name); \
    break;                                                              \
  default:                                                              \
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Not an integer \"%s\"", rep->str); \
  }

#define ICDB_GET_UINT16(icdb,rep,dest,name) ICDB_GET_UINT(icdb,rep,dest,name,UINT16_MAX)
#define ICDB_GET_UINT32(icdb,rep,dest,name) ICDB_GET_UINT(icdb,rep,dest,name,UINT32_MAX)
#define ICDB_GET_UINT64(icdb,rep,dest,name) ICDB_GET_UINT(icdb,rep,dest,name,UINT64_MAX)

#define CHECK_ICDB(icdb)  if (!(icdb) || !(icdb)->redisctx) {           \
    if ((icdb)) {                                                       \
      ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Null Redis context");         \
    }                                                                   \
    return ICDB_EPARAM;                                                 \
  }
#define CHECK_PARAM(icdb,param)  if (!(param)) {                    \
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Bad parameter "#param);     \
    return ICDB_FAILURE;                                            \
  }

/* /!\ if Redis reply is NULL, the context must be discarded */
#define CHECK_REP(icdb,rep)  if (!(rep)) {                              \
    ICDB_SET_STATUS(icdb, ICDB_EBADRESP, "Null DB response");           \
    return ICDB_FAILURE;                                                \
  } else if ((rep)->type == REDIS_REPLY_ERROR) {                        \
    ICDB_SET_STATUS(icdb, ICDB_EBADRESP, (rep)->str);                   \
    return ICDB_FAILURE;                                                \
  }

#define CHECK_REP_TYPE(icdb,rep,rtype) CHECK_REP(icdb, rep);            \
  if ((rep)->type != rtype) {                                           \
    ICDB_SET_STATUS(icdb, ICDB_EBADRESP, "Expected Redis response type %d, got %d", rtype, (rep)->type); \
    return ICDB_FAILURE;                                                \
  }


struct icdb_context {
  redisContext      *redisctx;
  int                status;
  char               errstr[ICDB_ERRSTR_LEN];
};


/* internal utility functions */

/**
 * Get an unsigned integer from a Redis reply "str" member into RES.
 *
 */
static int
_icdb_get_uint(const redisReply *rep, unsigned long long *res, unsigned long long max);


/**
 * Get a string from a Redis reply "str" member int DEST.
 *
 */
static int
_icdb_get_str(const redisReply *rep, char *dest, size_t maxlen);

static int
_icdb_set_status(struct icdb_context *icdb, int status,
                 const char *filename, int lineno, const char *funcname,
                 const char *format, ...);


/* public functions */

int
icdb_init(struct icdb_context **icdb_context)
{
  *icdb_context = NULL;

  struct icdb_context *icdb = calloc(1, sizeof(*icdb));
  if (!icdb)
    return ICDB_ENOMEM;

  icdb->status = ICDB_SUCCESS;

  icdb->redisctx = redisConnect("127.0.0.1", 6379);

  if (icdb->redisctx == NULL) {
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Null DB context");
    return ICDB_FAILURE;
  }

  if (icdb->redisctx->err) {
    icdb->status = ICDB_EPROTO;
    return ICDB_FAILURE;
  }

  *icdb_context = icdb;

  return ICDB_SUCCESS;
}


void
icdb_fini(struct icdb_context **icdb)
{
  if (icdb) {
      if (*icdb)
        redisFree((*icdb)->redisctx);
      free(*icdb);
  }
  *icdb = NULL;
}


char *
icdb_errstr(struct icdb_context *icdb)
{
  if (!icdb)
    return NULL;

  if (icdb->status == ICDB_EPROTO && icdb->redisctx) {
    return icdb->redisctx->errstr;
  } else if (icdb->status != ICDB_SUCCESS) {
    return icdb->errstr;
  } else {
    return NULL;
  }
}


int
icdb_command(struct icdb_context *icdb, const char *format, ...)
{
  CHECK_ICDB(icdb);

  if (!icdb->redisctx)
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Null database context");

  va_list ap;
  va_start(ap, format);
  redisReply *redisrep = redisvCommand(icdb->redisctx, format, ap);
  va_end(ap);

  if (redisrep == NULL)
    return ICDB_EPROTO;

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


/** "client" management */

int
icdb_getclient(struct icdb_context *icdb, const char *clid, struct icdb_client *client)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, client);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  rep = redisCommand(icdb->redisctx, "HGETALL client:%s", clid);

  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  if (rep->elements == 0) {
    ICDB_SET_STATUS(icdb, ICDB_NORESULT, "No client with id %s", clid);
    return ICDB_NORESULT;
  }

  /* HGETALL returns all keys followed by their respective value */
  for (size_t i = 0; i < rep->elements; i++) {
    char *key = rep->element[i]->str;
    redisReply *r = rep->element[++i];

    CHECK_REP_TYPE(icdb, r, REDIS_REPLY_STRING);

    if (!strcmp(key, "clid")) {
      ICDB_GET_STR(icdb, r, client->clid, key, UUID_STR_LEN);
    }
    else if (!strcmp(key, "type")) {
      ICDB_GET_STR(icdb, r, client->type, key, ICC_TYPE_LEN);
    }
    else if (!strcmp(key, "addr")) {
      ICDB_GET_STR(icdb, r, client->addr, key, ICC_ADDR_LEN);
    }
    else if (!strcmp(key, "provid")) {
      ICDB_GET_UINT16(icdb, r, &client->provid, key);
    }
    else if (!strcmp(key, "jobid")) {
      ICDB_GET_UINT32(icdb, r, &client->jobid, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
  }

  return icdb->status;
}


int
icdb_getclients(struct icdb_context *icdb, const char *type, uint32_t jobid,
                struct icdb_client *clients, size_t size, unsigned long long *count)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clients);

  redisReply *rep = NULL;

  *count = 0;

  /* XX filter! use SINTER for multiple filters */
  if (type && jobid) {
    ;
  } else if (type) {
    ;
  } else if (jobid) {
    ;
  } else {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients ALPHA DESC "
                       "GET client:*->clid "
                       "GET client:*->type "
                       "GET client:*->addr "
                       "GET client:*->provid "
                       "GET client:*->jobid");
    CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  }

  /* SORT returns fields one after the other, a client is 5 fields */
  /* XX use limit/offset at some point?*/
  *count = rep->elements / 5;

  assert(rep->elements % 5 == 0);

  if (size < *count) {
    ICDB_SET_STATUS(icdb, ICDB_E2BIG, "Too many clients to store");
    return ICDB_E2BIG;
  }

  size_t i, j;
  redisReply *r;
  for (i = 0; i < *count; i++) {
    for (j = 0; j < 5; j++) {
      r = rep->element[i * 5 + j];
      CHECK_REP_TYPE(icdb, r, REDIS_REPLY_STRING);
      switch (j) {
      case 0:
        ICDB_GET_STR(icdb, r, clients[i].clid, "clid", UUID_STR_LEN);
        break;
      case 1:
        ICDB_GET_STR(icdb, r, clients[i].type, "type", ICC_TYPE_LEN);
        break;
      case 2:
        _icdb_get_str(r, clients[i].addr, ICC_ADDR_LEN);
        /* ICDB_GET_STR(icdb, r, "addr", ICC_ADDR_LEN); */
        break;
      case 3:
        ICDB_GET_UINT16(icdb, r, &clients[i].provid,  "provid");
        break;
      case 4:
        ICDB_GET_UINT32(icdb, r, &clients[i].jobid, "jobid");
        break;
      }
      /* immediately stop processing if one field is in error */
      if (icdb->status != ICDB_SUCCESS) {
        return ICDB_FAILURE;
      }
    }
  }

  return ICDB_SUCCESS;
}


int
icdb_setclient(struct icdb_context *icdb, const char *clid,
               const char *type, const char *addr, uint16_t provid, uint32_t jobid)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, type);
  CHECK_PARAM(icdb, addr);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  /* XX add transaction around 1) and 2) */
  /* 1) write client to hashmap */
  rep = redisCommand(ctx, "HSET client:%s clid %s type %s addr %s provid %"PRIu32" jobid %"PRIu32,
                     clid, clid, type, addr, provid, jobid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  /* 2) write to client sets (~indexes)  */
  rep = redisCommand(ctx, "SADD index:clients %s", clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  rep = redisCommand(ctx, "SADD index:clients:type:%s %s", type, clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  rep = redisCommand(ctx, "SADD index:clients:jobid:%"PRIu32" %s", jobid, clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  return icdb->status;
}


int
icdb_delclient(struct icdb_context *icdb, const char *clid)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  /* Remove client and client index  */
  /* XX transaction? + separate check */
  rep = redisCommand(icdb->redisctx, "DEL client:%s", clid);
  rep = redisCommand(icdb->redisctx, "SREM index:clients %s", clid);

  /* DEL returns the number of keys that were deleted */
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
  if (rep->integer != 1) {
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Wrong number of client deleted: %d", rep->integer);
  }

  return icdb->status;
}


/** ICDB Utils */

static int
_icdb_set_status(struct icdb_context *icdb, int status,
                 const char *filename, int lineno, const char *funcname,
                 const char *format, ...)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, format);

  icdb->status = status;

  int nbytes;

  if (filename && lineno && funcname) {
    nbytes = snprintf(icdb->errstr, ICDB_ERRSTR_LEN, "%s (%s:%d): ", funcname, filename, lineno);
    if (nbytes < 0) {
      return ICDB_FAILURE;
    }
  }

  va_list ap;
  va_start(ap, format);

  if (nbytes < ICDB_ERRSTR_LEN) {
    vsnprintf(icdb->errstr + nbytes, ICDB_ERRSTR_LEN, format, ap);
    icdb->errstr[ICDB_ERRSTR_LEN - 1] = '\0';
  }

  va_end(ap);

  /* XX return value is not checked by calling macro */
  return ICDB_SUCCESS;
}


static int
_icdb_get_str(const redisReply *rep, char *dest, size_t maxlen)
{
  if (!rep || !dest || !(rep->str) || maxlen == 0) {
    return ICDB_EPARAM;
  }

  if (rep->type != REDIS_REPLY_STRING) {
    return ICDB_EPARAM;
  }

  if (rep->len + 1 > maxlen) {
    return ICDB_E2BIG;
  }

  strncpy(dest, rep->str, rep->len);
  dest[rep->len] = '\0';

  return ICDB_SUCCESS;
}

static int
_icdb_get_uint(const redisReply *rep, unsigned long long *res, unsigned long long max)
{
  if (!rep || !res || !rep->str) {
    return ICDB_EPARAM;
  }

  if (rep->type != REDIS_REPLY_STRING) {
    return ICDB_EPARAM;
  }

  char *end;
  errno = 0;

  *res = strtoull(rep->str, &end, 0);

  if (errno != 0 || end == rep->str || *end != '\0') {
    return ICDB_FAILURE;
  }
  else if (*res > max) {
    return ICDB_E2BIG;
  }

  return ICDB_SUCCESS;
}
