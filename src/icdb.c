#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIuXX */
#include <stdlib.h>             /* malloc */
#include <string.h>             /* strncpy */
#include <hiredis.h>
#include <margo.h>
#include <unistd.h>             /* sleep */ // CHANGE: JAVI
#include <time.h>


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

#define ICDB_GET_STRDUP(icdb,rep,dest,name) switch (_icdb_get_strdup(rep, dest)) { \
  case ICDB_SUCCESS:                                                    \
    ICDB_SET_STATUS(icdb, ICDB_SUCCESS, "Success");                     \
    break;                                                              \
  case ICDB_EPARAM:                                                     \
    ICDB_SET_STATUS(icdb, ICDB_EPARAM, "Bad parameters for %s", name);  \
    break;                                                              \
  case ICDB_ENOMEM:                                                     \
    ICDB_SET_STATUS(icdb, ICDB_ENOMEM, "Out of memory field %s", name); \
    break;                                                              \
  default:                                                              \
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Failure");                     \
  }

#define ICDB_GET_INT(icdb,rep,dest,name,fmt) {                         \
    CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_STRING);                      \
    int _n = sscanf(rep->str, "%"fmt, dest);                            \
    if (_n == 1)                                                        \
      ICDB_SET_STATUS(icdb, ICDB_SUCCESS, "Success");                   \
    else if (errno)                                                     \
      ICDB_SET_STATUS(icdb, ICDB_FAILURE, "Conversion error for %s: %s", name, strerror(errno)); \
    else                                                                \
      ICDB_SET_STATUS(icdb, ICDB_FAILURE, "No conversion possible for %s", name); \
  }

#define ICDB_GET_UINT16(icdb,rep,dest,name)  ICDB_GET_INT(icdb,rep,dest,name,SCNu16)
#define ICDB_GET_UINT32(icdb,rep,dest,name)  ICDB_GET_INT(icdb,rep,dest,name,SCNu32)
#define ICDB_GET_UINT64(icdb,rep,dest,name)  ICDB_GET_INT(icdb,rep,dest,name,SCNu64)
#define ICDB_GET_INT32(icdb,rep,dest,name)  ICDB_GET_INT(icdb,rep,dest,name,SCNi32)

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

#define ICDB_CLIENT_QUERY "GET client:*->clid " \
  "GET client:*->type "                         \
  "GET client:*->addr "                         \
  "GET client:*->nodelist "                     \
  "GET client:*->provid "                       \
  "GET client:*->jobid "                        \
  "GET client:*->nprocs "                       \
  "GET client:*->reconfig_nprocs "              \
  "GET client:*->reconfig_nnodes"


struct icdb_context {
  redisContext      *redisctx;
  int                status;
  char               errstr[ICDB_ERRSTR_LEN];
};

// CHANGE: JAVI
static ABT_mutex_memory icdb_mutex = ABT_MUTEX_INITIALIZER;
// END CHANGE: JAVI

/* internal utility functions */
/**
 * Get a string from a Redis reply "str" member int DEST.
 * The strdup version does the memory allocation, and the result
 * must be freed by the caller.
 */
static int
_icdb_get_str(const redisReply *rep, char *dest, size_t maxlen);
static int
_icdb_get_strdup(const redisReply *rep, char **dest);

static int
_icdb_set_status(struct icdb_context *icdb, int status,
                 const char *filename, int lineno, const char *funcname,
                 const char *format, ...);
static int
client_set(struct icdb_context *icdb, redisReply **rep, struct icdb_client *c);


/* public functions */

int
icdb_init(struct icdb_context **icdb_context, char *ip_addr)
{
  *icdb_context = NULL;

  if (ip_addr == NULL)
    return ICDB_EPARAM;

  struct icdb_context *icdb = calloc(1, sizeof(*icdb));
  if (!icdb)
    return ICDB_ENOMEM;

  icdb->status = ICDB_SUCCESS;

  icdb->redisctx = redisConnect(ip_addr, 6379);

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

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  redisReply *rep;
  rep = redisCommand(icdb->redisctx, "HGETALL client:%s", clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI

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
    else if (!strcmp(key, "nodelist")) {
      ICDB_GET_STR(icdb, r, client->nodelist, key, ICDB_NODELIST_LEN);
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
    else if (!strcmp(key, "reconfig_ncpus")) {
      ICDB_GET_INT32(icdb, r, &client->reconfig_nprocs, key);
    }
    else if (!strcmp(key, "reconfig_nnodes")) {
      ICDB_GET_INT32(icdb, r, &client->reconfig_nnodes, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
  }

  return icdb->status;
}

int
icdb_getlargestclient(struct icdb_context *icdb, struct icdb_client *client)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, client);

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  redisReply *rep = redisCommand(icdb->redisctx,
                    "SORT index:clients DESC BY client:*->nnodes LIMIT 0 1 "
                    ICDB_CLIENT_QUERY);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
    
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  return client_set(icdb, rep->element, client);
}

int
icdb_getclients(struct icdb_context *icdb, uint32_t jobid,
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


  /* XX if multiple filters use SINTER */
  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  if (jobid) {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients:jobid:%"PRIu32" ALPHA DESC "
                       ICDB_CLIENT_QUERY, jobid);

  } else {
    rep = redisCommand(icdb->redisctx,
                       "SORT index:clients ALPHA DESC "
                       ICDB_CLIENT_QUERY);
  }
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI

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
  for (size_t i = 0; i < *count; i++) {
    int r = client_set(icdb, rep->element + i * ICDB_CLIENT_NFIELDS, &clients[i]);
    if (r != ICDB_SUCCESS) {
      return r;
    }
  }
  return ICDB_SUCCESS;
}


// CHANGE: JAVI
int
icdb_addnodes(struct icdb_context *icdb, const char *clid, const char *nodelist)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, nodelist);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  /* XX fixme: wrap in transaction */
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);

  /* parse comma-separated node lists and add them to the nodelist */
  uint32_t nnodes = 0;
  char *l = strdup(nodelist);
  if (!l) { return ICDB_ENOMEM; }

  char *node = strtok(l, ",");
  do {
    nnodes++;
    ABT_mutex_lock(mutex);
    rep = redisCommand(ctx, "RPUSH nodelist:client:%s %s", clid, node);
    ABT_mutex_unlock(mutex);
    CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
  } while ((node = strtok(NULL, ",")));
  free(l);
  freeReplyObject(rep);

  return icdb->status;
}

int
icdb_delnodes(struct icdb_context *icdb, const char *clid, const char *nodelist)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, nodelist);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep = NULL;
  redisContext *ctx = icdb->redisctx;

  /* XX fixme: wrap in transaction */
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);

  /* parse comma-separated node lists and add them to the nodelist */
  uint32_t nnodes = 0;
  char *l = strdup(nodelist);
  if (!l) { return ICDB_ENOMEM; }

  char *node = strtok(l, ",");
  do {
    nnodes++;
    ABT_mutex_lock(mutex);
    rep = redisCommand(ctx, "LREM nodelist:client:%s 0 %s", clid, node);
    ABT_mutex_unlock(mutex);
    CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
  } while ((node = strtok(NULL, ",")));
  free(l);
  freeReplyObject(rep);

  return icdb->status;
}

int
icdb_getMonitor(struct icdb_context *icdb, const char *clid, double *rate_cpu, double *rate_mem, int *num_proc, double *rtime, double *ptime, double *ctime)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, rate_cpu);
  CHECK_PARAM(icdb, rate_mem);
  CHECK_PARAM(icdb, num_proc);
  CHECK_PARAM(icdb, rtime);
  CHECK_PARAM(icdb, ptime);
  CHECK_PARAM(icdb, ctime);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep = NULL;
  redisReply *rep2 = NULL;
  redisContext *ctx = icdb->redisctx;
  double rate_cpu_total=0.0, rate_mem_total=0.0;
  int num_nodes=0;
    
    

  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
    
  //ABT_mutex_lock(mutex);
  //rep = redisCommand(ctx, "KEYS *", clid);
  //ABT_mutex_unlock(mutex);
  //fprintf(stderr, "icdb_getMonitor: step 0: KEYS *\n");
  //CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  //for (size_t i = 0; i < rep->elements; i++) {
  //    fprintf(stderr, "icdb_getMonitor: KEYS: %s\n",rep->element[i]->str);
  //}
  //freeReplyObject(rep);

  // get list of nodes for clid
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "LRANGE nodelist:client:%s 0 -1", clid);
  ABT_mutex_unlock(mutex);
  //fprintf(stderr, "icdb_getMonitor: step 1: LRANGE nodelist:client:%s 0 -1 (%p)\n",clid, rep);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  int memory=0, ncpu=0, ncores=0;
  double rate_mem_local=0.0, rate_cpu_local=0.0;

  // get monitor values for each node of clid
  for (size_t i = 0; i < rep->elements; i++) {
    ABT_mutex_lock(mutex);
    rep2 = redisCommand(ctx, "GET monitor:%s", rep->element[i]->str);
    ABT_mutex_unlock(mutex);
    //fprintf(stderr, "icdb_getMonitor: step 2: GET monitor:%s\n", rep->element[i]->str);
    CHECK_REP_TYPE(icdb, rep2, REDIS_REPLY_STRING);
    char ip_addr[20];
    int nfields =  sscanf(rep2->str, "%[^ ] %d %lf %d %d %lf", ip_addr, &memory, &rate_mem_local, &ncpu, &ncores, &rate_cpu_local);
    freeReplyObject(rep2);
    if (nfields != 6) {
        fprintf(stderr, "icdb_getMonitor: Error with sscanf of GET monitor:%s\n",rep->element[i]->str);
        icdb->status = ICDB_ENOMEM;
        freeReplyObject(rep);
        goto end;
    }
    rate_cpu_total = rate_cpu_total + rate_cpu_local;
    rate_mem_total = rate_mem_total + rate_mem_local;
    num_nodes++;
  }
  freeReplyObject(rep);

  // get current iteration key for clid
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "KEYS monitorFlexMPI:%s:*:current", clid);
  ABT_mutex_unlock(mutex);
  //fprintf(stderr, "icdb_getMonitor: step 3: KEYS monitorFlexMPI:%s:*:current\n", clid);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  if (rep->elements != 1) {
    fprintf(stderr, "icdb_getMonitor: KEYS monitorFlexMPI:%s:*:current, there is  not just a single entry\n",clid);
    icdb->status = ICDB_EBADRESP;
    freeReplyObject(rep);
    goto end;
  }
  //fprintf(stderr, "icdb_getMonitor: step 4\n");
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_STRING);
  char *iter_key = strdup(rep->element[0]->str);
  freeReplyObject(rep);

  // get current iteration data for clid
  double aux_rtime=0-0, aux_ptime=0.0, aux_ctime=0.0;
  int aux_num_proc=0;
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "GET %s", iter_key);
  ABT_mutex_unlock(mutex);
  //fprintf(stderr, "icdb_getMonitor: step 5: GET %s\n", iter_key);
  free(iter_key);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_STRING);
  int nfields =  sscanf(rep->str, "%*d %*f %lf %lf %lf %*f %d", &aux_rtime, &aux_ptime, &aux_ctime, &aux_num_proc);
  freeReplyObject(rep);
  if (nfields != 4) {
    fprintf(stderr, "icdb_getMonitor: Error with sscanf of GET %s\n",iter_key);
    icdb->status = ICDB_ENOMEM;
    goto end;
  }

    
  // calculate final rates
  (*rate_cpu) = rate_cpu_total / ((double) num_nodes);
  (*rate_mem) = rate_mem_total / ((double) num_nodes);
  (*num_proc) = ncpu * ncores;
  //(*num_proc) = aux_num_proc;
  (*rtime) = aux_rtime;
  (*ptime) = aux_ptime;
  (*ctime) = aux_ctime;


  // store data as log
  char val_str[256];
  sprintf(val_str, "%ld %d %d %lf %lf %lf %lf %lf", time(NULL), num_nodes, (*num_proc), (*rate_cpu), (*rate_mem), (*rtime), (*ptime), (*ctime));

  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "RPUSH log:%s %s", clid, val_str);
  ABT_mutex_unlock(mutex);
  //fprintf(stderr, "icdb_getMonitor: step 5: RPUSH log:%s %s\n",clid, val_str);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
  freeReplyObject(rep);
    
end:
  return icdb->status;
}
// END CHANGE: JAVI


int
icdb_setclient(struct icdb_context *icdb, const char *clid,
               const char *type, const char *addr, const char *nodelist,
               uint16_t provid, uint32_t jobid, uint32_t jobncpus,
               const char *jobnodelist, uint64_t nprocs)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);
  CHECK_PARAM(icdb, type);
  CHECK_PARAM(icdb, addr);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  /* XX fixme: wrap in transaction */
  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  // END CHANGE: JAVI

  /* ALBERTO - Check if client exisits and update his addr*/
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "HMGET client:%s addr", clid);
  ABT_mutex_unlock(mutex);
  //CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  if (rep == NULL || rep->type == REDIS_REPLY_ERROR) 
        fprintf(stderr, "[DEBUG] Error getting client address from redis");

  //fprintf(stderr, "[DEBUG] Get client addr - num elements returned= %lu - addr = %s\n", rep->elements, rep->element[0]->str);
  if (rep->type == REDIS_REPLY_ARRAY && rep->elements == 1 && rep->element[0]->str != NULL){
  //if (rep->elements > 0 && rep->str != NULL){ 
    //fprintf(stderr, "[DEBUG] Reloading client address\n");
    ABT_mutex_lock(mutex);
    rep = redisCommand(ctx, "HSET client:%s addr %s", clid, addr);
    ABT_mutex_unlock(mutex);
    if (rep == NULL || rep->type == REDIS_REPLY_ERROR) 
        fprintf(stderr, "[DEBUG] Error updating the client addr\n");
    return ICDB_SUCCESS;
  } 

  //fprintf(stderr, "[DEBUG] Client registered normally\n");

  /* parse comma-separated node lists and add them to the nodelist */
  uint32_t nnodes = 0, jobnnodes = 0;
  char *l = strdup(jobnodelist);
  if (!l) { return ICDB_ENOMEM; }

  char *node = strtok(l, ",");
  if (node) {
    do {
      nnodes++;
      // CHANGE: JAVI
      ABT_mutex_lock(mutex);
      rep = redisCommand(ctx, "RPUSH nodelist:client:%s %s", clid, node);
      ABT_mutex_unlock(mutex);
      // END CHANGE: JAVI
      CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
    } while ((node = strtok(NULL, ",")));
  }
  free(l);

  l = strdup(jobnodelist);
  if (!l) { return ICDB_ENOMEM; }

  node = strtok(l, ",");
  if (node) {
    do {
      jobnnodes++;
      // CHANGE: JAVI
      ABT_mutex_lock(mutex);
      rep = redisCommand(ctx, "RPUSH nodelist:job:%"PRIu32" %s", jobid, node);
      ABT_mutex_unlock(mutex);
      // END CHANGE: JAVI
      CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);
    } while ((node = strtok(NULL, ",")));
  }
  free(l);

  /* 1) Create or update job */
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "HSET job:%"PRIu32" jobid %"PRIu32" ncpus %"PRIu32" nnodes %"PRIu32" nodelist %s",jobid, jobid, jobncpus, jobnnodes, jobnodelist);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  /* 2) write client to hashmap */
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "HSET client:%s clid %s type %s addr %s nnodes %"PRIu32" nodelist %s provid %"PRIu32" jobid %"PRIu32" nprocs %"PRIu64" reconfig_nprocs 0 reconfig_nnodes 0", clid, clid, type, addr, nnodes, nodelist, provid, jobid, nprocs);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  /* 3) write to client sets (~indexes)  */
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "SADD index:clients %s", clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "SADD index:clients:type:%s %s", type, clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "SADD index:clients:jobid:%"PRIu32" %s", jobid, clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
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
  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "HMGET client:%s jobid type", clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
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
  /* DEL & SREM return the number of keys that were deleted */

  /* begin transaction */
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "MULTI");
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  assert(rep->type == REDIS_REPLY_STATUS);

  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  redisCommand(icdb->redisctx, "SREM index:clients:jobid:%"PRIu32" %s", *jobid, clid);
  redisCommand(icdb->redisctx, "SREM index:clients:type:%s %s", type, clid);
  redisCommand(icdb->redisctx, "SREM index:clients %s", clid);
  redisCommand(icdb->redisctx, "DEL client:%s", clid);
  redisCommand(icdb->redisctx, "DEL nodelist:client:%s", clid);
  redisCommand(icdb->redisctx, "DEL nodelist:job:%"PRIu32, *jobid);
  redisCommand(icdb->redisctx, "DEL client:%s:reconfig", clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI

  /* end transaction & check responses */
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "EXEC");
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[1], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[2], REDIS_REPLY_INTEGER); /* SREM */
  CHECK_REP_TYPE(icdb, rep->element[3], REDIS_REPLY_INTEGER); /* DEL  */
  CHECK_REP_TYPE(icdb, rep->element[4], REDIS_REPLY_INTEGER); /* DEL  */
  CHECK_REP_TYPE(icdb, rep->element[5], REDIS_REPLY_INTEGER); /* DEL  */
  CHECK_REP_TYPE(icdb, rep->element[6], REDIS_REPLY_INTEGER); /* DEL  */

  return icdb->status;
}

int
icdb_reconfigurable(struct icdb_context *icdb, const char *clid, int32_t procs_hint, int32_t nodes_hint)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clid);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  rep = redisCommand(ctx, "HSET client:%s reconfig_nprocs %"PRIi32" reconfig_nnodes %"PRIi32, clid, procs_hint, nodes_hint);
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

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
  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "SMEMBERS index:clients:jobid:%"PRIu32, jobid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
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
  // CHANGE: JAVI
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "DEL job:%"PRIu32, jobid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_INTEGER);

  return icdb->status;
}

int
icdb_getclients2(struct icdb_context *icdb, uint32_t jobid, const char *type,
                struct icdb_client *clients[], size_t *count, uint64_t *cursor)
{
  CHECK_ICDB(icdb);
  CHECK_PARAM(icdb, clients);

  redisReply *rep = NULL;

  if (jobid && type) {
    /* XX sinter return length is unbounded, no cursor */
    rep = redisCommand(icdb->redisctx,
            "SINTER index:clients:jobid:%"PRIu32" index:clients:type:%s", jobid, type);
  } else if (jobid) {
     rep = redisCommand(icdb->redisctx, "SSCAN index:clients:jobid:%"PRIu32" %d", jobid, *cursor);
  } else if (type) {
     rep = redisCommand(icdb->redisctx, "SSCAN index:clients:type:%s %d", type, *cursor);
  } else {
     rep = redisCommand(icdb->redisctx, "SSCAN index:clients %d", *cursor);
  }

  /* SSCAN returns the cursor + an array of elements */
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);
  CHECK_REP_TYPE(icdb, rep->element[0], REDIS_REPLY_STRING)
  CHECK_REP_TYPE(icdb, rep->element[1], REDIS_REPLY_ARRAY)

  ICDB_GET_UINT64(icdb, rep->element[0], cursor, "cursor");
  *count = rep->element[1]->elements;

  struct icdb_client *tmp = reallocarray(*clients, *count, sizeof(struct icdb_client));
  if (!tmp) {
    return ICDB_ENOMEM;
  }

  /* XX check return code */
  for (size_t i = 0; i < *count; i++) {
    icdb_getclient(icdb, rep->element[1]->element[i]->str, &(tmp[i]));
  }
  *clients = tmp;

  return ICDB_SUCCESS;
}


int icdb_shrink(struct icdb_context *icdb, char *clid, char **newnodelist)
{
  CHECK_ICDB(icdb);
  icdb->status = ICDB_SUCCESS;

  redisReply *rep;

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "EVAL %s 1 nodelist:client:%s",
    "local n = math.floor(redis.call('LLEN', KEYS[1]) / 2) "
    "return table.concat(redis.call('LRANGE', KEYS[1], 0, n-1), ',')",
    clid);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_STRING);

  *newnodelist = strdup(rep->str);
  return icdb->status;
}

int icdb_incrnprocs(struct icdb_context *icdb, char *clid, int64_t incrby)
{
  CHECK_ICDB(icdb);
  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
    
  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(icdb->redisctx, "HINCRBY client:%s nprocs %"PRId64, clid, incrby);
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  return icdb->status;
}

void
icdb_job_init(struct icdb_job *job) {
  if (!job) return;
  job->jobid = 0;
  job->nnodes = 0;
  job->ncpus = 0;
  job->nodelist = NULL;
}

void
icdb_job_free(struct icdb_job **job) {
  if (!job || !*job) return;
  struct icdb_job *j = *job;
  j->jobid = 0;
  j->nnodes = 0;
  j->ncpus = 0;
  if (j->nodelist) {
    free(j->nodelist);
  }
  j->nodelist = NULL;
  j = NULL;
}

int
icdb_getjob(struct icdb_context *icdb, uint32_t jobid, struct icdb_job *job)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  // CHANGE: JAVI
  //assert(-1==0);
  fprintf(stderr, "icdb_getjob: jobid=%d\n", jobid);
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "HGETALL job:%"PRIu32, jobid);
  ABT_mutex_unlock(mutex);
  //assert(1==0);
  // END CHANGE: JAVI
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
    else if (!strcmp(key, "ncpus")) {
      ICDB_GET_UINT32(icdb, r, &job->ncpus, key);
    }
    else if (!strcmp(key, "nodelist")) {
      ICDB_GET_STRDUP(icdb, r, &job->nodelist, key);
    }

    if (icdb->status != ICDB_SUCCESS)
      break;
  }

  return icdb->status;
}

/*
 * New Redis stuff: store jobs and use message queue
 */

int
icdb_getlargestjob(struct icdb_context *icdb, uint32_t *jobid) {
 CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;
  *jobid = 0;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  ABT_mutex_lock(mutex);
  rep = redisCommand(ctx, "SORT admire:jobs:running DESC LIMIT 0 1 "
                          "BY admire:job:*->nnodes "
                          "GET admire:job:*->jobid");
  ABT_mutex_unlock(mutex);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  if (rep->elements > 0) {
    ICDB_GET_UINT32(icdb, rep->element[0], jobid, "jobid");
  }

  return icdb->status;
}

/* Message stream */
int
icdb_mstream_read(struct icdb_context *icdb, char *streamkey)
{
  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  do {
    ABT_mutex_lock(mutex);
    //rep = redisCommand(ctx, "XREAD BLOCK 0 STREAMS %s $", streamkey);
    rep = redisCommand(ctx, "XREAD STREAMS %s $", streamkey);
    ABT_mutex_unlock(mutex);
      if ((rep)->type == REDIS_REPLY_NIL) sleep(1);
  } while ((rep)->type == REDIS_REPLY_NIL);
  // END CHANGE: JAVI

  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  /* XREAD returns an array of arrays:
     1) 1) "mystream"
        2) 1) 1) "1690215474757-0"
              2) 1) "foo"
                 2) "bar"
  */
  for (size_t s = 0; s < rep->elements; s++) {	/* iterate over streams */
    redisReply *r;
    r = rep->element[s];
    fprintf(stderr, "stream %s\n", r->element[0]->str);
	size_t nmsg = r->element[1]->elements;

    for (size_t m = 0; m < nmsg; m++) {			/* iterate over messages */
      r = r->element[1]->element[m];
      fprintf(stderr, "  message %s\n", r->element[0]->str);
      size_t nkv = r->element[1]->elements;

      for (size_t i = 0; i < nkv; i++) {		/* iterate over key val */
        char *key = r->element[1]->element[i]->str;
        char *val = r->element[1]->element[++i]->str;
        printf("    %s: %s\n", key, val);
      }
    }
  }
  return icdb->status;
}

/* Beegfs status message stream */
#define BEEGFS_MSTREAM "admire:beegfs:status"

int
icdb_mstream_beegfs(struct icdb_context *icdb, struct icdb_beegfs *result)
{
  result->qlen = 0;
  result->timestamp = 0;

  CHECK_ICDB(icdb);

  icdb->status = ICDB_SUCCESS;

  redisReply *rep;
  redisContext *ctx = icdb->redisctx;

  // CHANGE: JAVI
  ABT_mutex mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&icdb_mutex);
  do {
    ABT_mutex_lock(mutex);
    //rep = redisCommand(ctx, "XREAD BLOCK 0 STREAMS " BEEGFS_MSTREAM " $");
    rep = redisCommand(ctx, "XREAD STREAMS " BEEGFS_MSTREAM " $");
    ABT_mutex_unlock(mutex);
      if ((rep)->type == REDIS_REPLY_NIL) sleep(1);
  } while ((rep)->type == REDIS_REPLY_NIL);
  // END CHANGE: JAVI
  CHECK_REP_TYPE(icdb, rep, REDIS_REPLY_ARRAY);

  /*
   * XREAD returns an array of arrays
   * we only care about the last message of the one stream
   */
  size_t n;

  if (rep->elements != 1) {            /* 1 stream only */
    ICDB_SET_STATUS(icdb, ICDB_FAILURE, "got %lld streams admire:beegfs", rep->elements);
  }
  rep = rep->element[0];
  n = rep->element[1]->elements;
  rep = rep->element[1]->element[n-1];  /* last message */

  n = rep->element[1]->elements;
  for (size_t i = 0; i < n; i++) {
    char *key = rep->element[1]->element[i]->str;
    redisReply *val = rep->element[1]->element[++i];

    if (!strcmp(key, "timestamp")) {
       ICDB_GET_UINT64(icdb, val, &result->timestamp, "timestamp")
    } else if (!strcmp(key, "qlen")) {
      ICDB_GET_UINT32(icdb, val, &result->qlen, "qlen")
    }
    if (icdb->status != ICDB_SUCCESS) {
      return ICDB_FAILURE;
    }
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

static int
_icdb_get_strdup(const redisReply *rep, char **dest)
{
  if (!rep || !(rep->str)) {
    return ICDB_EPARAM;
  }

  if (rep->type != REDIS_REPLY_STRING) {
    return ICDB_EPARAM;
  }

  *dest = strdup(rep->str);
  if (!dest) {
  	return ICDB_ENOMEM;
  }

  return ICDB_SUCCESS;
}

static int
client_set(struct icdb_context *icdb, redisReply **rep, struct icdb_client *c)
{
  for (int i = 0; i < ICDB_CLIENT_NFIELDS; i++) {
    redisReply *r = rep[i];
    CHECK_REP_TYPE(icdb, r, REDIS_REPLY_STRING);
    switch (i) {
    case 0:
      ICDB_GET_STR(icdb, r, c->clid, "clid", UUID_STR_LEN);
      break;
    case 1:
      ICDB_GET_STR(icdb, r, c->type, "type", ICC_TYPE_LEN);
      break;
    case 2:
      _icdb_get_str(r, c->addr, ICC_ADDR_LEN);
      break;
    case 3:
      /* will fail if nodelist is too long */
      ICDB_GET_STR(icdb, r, c->nodelist, "nodelist", ICDB_NODELIST_LEN);
      break;
    case 4:
      ICDB_GET_UINT16(icdb, r, &c->provid,  "provid");
      break;
    case 5:
      ICDB_GET_UINT32(icdb, r, &c->jobid, "jobid");
      break;
    case 6:
      ICDB_GET_UINT64(icdb, r, &c->nprocs, "nprocs");
      break;
    case 7:
      ICDB_GET_INT32(icdb, r, &c->reconfig_nprocs, "reconfig_ncpus");
      break;
    case 8:
      ICDB_GET_INT32(icdb, r, &c->reconfig_nnodes, "reconfig_nnodes");
      break;
    }
    /* immediately stop processing if one field is in error */
    if (icdb->status != ICDB_SUCCESS) {
      return ICDB_FAILURE;
    }
  }
  return ICDB_SUCCESS;
}
