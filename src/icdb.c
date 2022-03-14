#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIuXX */
#include <stdlib.h>             /* malloc */
#include <string.h>             /* strncpy */
#include <hiredis.h>
#include "uuid_admire.h"        /* UUID_STR_LEN */

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

#define ICDB_GET_UINT(icdb,rep,dest,name,fmt) {                         \
    CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_STRING);                      \
    int _n = sscanf(rep->str, "%"fmt, dest);                            \
    if (_n == 1)                                                        \
      ICDB_SET_STATUS(icdb, ICDB_SUCCESS, "Success");                   \
    else if (errno)                                                     \
      ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Conversion error for %s: %s", name, strerror(errno)); \
    else                                                                \
      ICDB_SET_STATUS(icdb, ICDB_FAILURE, "No conversion possible for %s", name); \
  }

#define ICDB_GET_UINT16(icdb,rep,dest,name)  ICDB_GET_UINT(icdb,rep,dest,name,SCNu16)
#define ICDB_GET_UINT32(icdb,rep,dest,name)  ICDB_GET_UINT(icdb,rep,dest,name,SCNu32)
#define ICDB_GET_UINT64(icdb,rep,dest,name)  ICDB_GET_UINT(icdb,rep,dest,name,SCNu64)

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

#define ICDB_CLIENT_QUERY  " ALPHA DESC "       \
  "GET client:*->clid "                         \
  "GET client:*->type "                         \
  "GET client:*->addr "                         \
  "GET client:*->provid "                       \
  "GET client:*->jobid "                        \
  "GET client:*->nprocs"


struct icdb_context {
  redisContext      *redisctx;
  int                status;
  char               errstr[ICDB_ERRSTR_LEN];
};


/* internal utility functions */
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
    else if (!strcmp(key, "nprocs")) {
      ICDB_GET_UINT64(icdb, r, &client->nprocs, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
  }

  return icdb->status;
}


int
icdb_getclients(struct icdb_context *icdb, const char *type, uint32_t jobid,
                struct icdb_client *clients, size_t *count)
{
  /*
   * XX For now we return every clients at once. The thinking is
   * that this query is mainly used with a jobid filter, and there
   * is not an unbounded number of clients per job. Maybe at some
   * point a cursor might be more appropriate
   */
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clients);

  redisReply *rep = NULL;
  size_t need;


  if (type && jobid) {
    /* XX FIXME use SINTER for multiple filters */
    ;
  } else if (type) {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients:type:%s"
                       ICDB_CLIENT_QUERY, type);
  } else if (jobid) {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients:jobid:%"PRIu32
                       ICDB_CLIENT_QUERY, jobid);

  } else {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients"
                       ICDB_CLIENT_QUERY);
  }
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  /* SORT returns fields one after the other, a client is
     ICDB_CLIENT_NFIELDS fields */
  assert(rep->elements % ICDB_CLIENT_NFIELDS == 0);

  need = rep->elements / ICDB_CLIENT_NFIELDS;

  if (*count < need) {
    *count = need;
    ICDB_SET_STATUS(icdb, ICDB_E2BIG, "Too many clients to store");
    return ICDB_E2BIG;
  }

  *count = need;

  size_t i, j;
  redisReply *r;
  for (i = 0; i < *count; i++) {
    for (j = 0; j < ICDB_CLIENT_NFIELDS; j++) {
      r = rep->element[i * ICDB_CLIENT_NFIELDS + j];
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
      case 5:
        ICDB_GET_UINT64(icdb, r, &clients[i].nprocs, "nprocs");
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
               const char *type, const char *addr, uint16_t provid,
               uint32_t jobid, uint32_t jobntasks, uint32_t jobnnodes,
               uint64_t nprocs)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, type);
  CHECK_PARAM(icdb, addr);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  /* XX fixme: wrap in transaction */

  /* 1) Create or update job */
  rep = redisCommand(ctx, "HSET job:%"PRIu32" jobid %"PRIu32" ntasks %"PRIu32" nnodes %"PRIu32,
                     jobid, jobid, jobntasks, jobnnodes);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  /* 2) write client to hashmap */
  rep = redisCommand(ctx, "HSET client:%s clid %s type %s addr %s provid %"PRIu32" jobid %"PRIu32" nprocs %"PRIu64, clid, clid, type, addr, provid, jobid, nprocs);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  /* 3) write to client sets (~indexes)  */
  rep = redisCommand(ctx, "SADD index:clients %s", clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  rep = redisCommand(ctx, "SADD index:clients:type:%s %s", type, clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  rep = redisCommand(ctx, "SADD index:clients:jobid:%"PRIu32" %s", jobid, clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  return icdb->status;
}


int
icdb_delclient(struct icdb_context *icdb, const char *clid, uint32_t *jobid)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  char *type;

  rep = redisCommand(icdb->redisctx, "HMGET client:%s jobid type", clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  if (rep->elements == 0) {
    ICDB_SET_STATUS(icdb, ICDB_NORESULT, "No client with id %s", clid);
    return ICDB_NORESULT;
  }

  /* HMGET gets values in order */
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_STRING);
  CHECK_REP_TYPE(icdb, rep->element[1], REDIS_REPLY_STRING);

  ICDB_GET_UINT32(icdb, rep->element[0], jobid, "jobid");
  type = rep->element[1]->str;

  /* Remove client and client index  */
  /* XX fixme: wrap in transaction */
  /* DEL & SREM return the number of keys that were deleted */

  /* begin transaction */
  rep = redisCommand(icdb->redisctx, "MULTI");
  assert(rep->type == REDIS_REPLY_STATUS);

  redisCommand(icdb->redisctx, "SREM index:clients:jobid:%"PRIu32" %s", *jobid, clid);
  redisCommand(icdb->redisctx, "SREM index:clients:type:%s %s", type, clid);
  redisCommand(icdb->redisctx, "SREM index:clients %s", clid);
  redisCommand(icdb->redisctx, "DEL client:%s", clid);

  /* end transaction & check responses */
  rep = redisCommand(icdb->redisctx, "EXEC");
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[1], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_INTEGER); /* DEL  */

  return icdb->status;
}


int
icdb_deljob(struct icdb_context *icdb, uint32_t jobid)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  int ret;

  /* 1. delete clients associated with jobid */
  rep = redisCommand(icdb->redisctx, "SMEMBERS index:clients:jobid:%"PRIu32, jobid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  for (size_t i = 0; i < rep->elements; i++) {
    uint32_t _jobid;
    ret = icdb_delclient(icdb, rep->element[i]->str, &_jobid);
    if (ret != ICDB_SUCCESS) {
      return ret;
    }
    assert(_jobid == jobid);
  }

  /* 2. delete jobid */
  rep = redisCommand(icdb->redisctx, "DEL job:%"PRIu32, jobid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  return icdb->status;
}


int icdb_incrnprocs(struct icdb_context *icdb, char *clid, int64_t incrby) {
  CHECK_ICDB(icdb);
  icdb->status = ICDB_SUCCESS;

  redisReply *rep;

  rep = redisCommand(icdb->redisctx, "HINCRBY client:%s nprocs %"PRId64, clid, incrby);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  return icdb->status;
}


int
icdb_getjob(struct icdb_context *icdb, uint32_t jobid, struct icdb_job *job)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  rep = redisCommand(ctx, "HGETALL job:%"PRIu32, jobid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  /* HGETALL returns all keys followed by their respective value */
  for (size_t i = 0; i < rep->elements; i++) {
    char *key = rep->element[i]->str;
    redisReply *r = rep->element[++i];

    CHECK_REP_TYPE(icdb, r, REDIS_REPLY_STRING);

    if (!strcmp(key, "jobid")) {
      ICDB_GET_UINT32(icdb, r, &job->jobid, key);
    }
    else if (!strcmp(key, "nnodes")) {
      ICDB_GET_UINT32(icdb, r, &job->nnodes, key);
    }
    else if (!strcmp(key, "ntasks")) {
      ICDB_GET_UINT32(icdb, r, &job->ntasks, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
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

  int nbytes = 0;
  if (filename && lineno && funcname) {
    nbytes = snprintf(icdb->errstr, ICDB_ERRSTR_LEN, "%s (%s:%d): ", funcname, filename, lineno);
    if (nbytes < 0) {
      icdb->errstr[0] = '\0';
      return ICDB_FAILURE;
    }
  }

  va_list ap;
  va_start(ap, format);

  if (nbytes < ICDB_ERRSTR_LEN) {
    vsnprintf(icdb->errstr + nbytes, ICDB_ERRSTR_LEN, format, ap);
  }

  icdb->errstr[ICDB_ERRSTR_LEN - 1] = '\0';

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
