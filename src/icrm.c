#include <assert.h>
#include <netdb.h>              /* socket */
#include <stdarg.h>             /* va_ stuff */
#include <stdlib.h>             /* calloc */
#include <stdint.h>             /* uintXX_t */
#include <string.h>             /* strncpy */
#include <sys/types.h>          /* socket */
#include <sys/socket.h>         /* socket */

#include <abt.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "icc_priv.h"
#include "icrm.h"

#define CHECK_NULL(p)  if (!(p)) { return ICRM_EPARAM; }
#define CHECK_ICRM(icrm)  { CHECK_NULL(icrm); CHECK_NULL((icrm)->errstr); }

#define WRITERR(icrm,...)  _writerr(icrm, __FILE__, __LINE__, __func__, __VA_ARGS__)


static enum icrm_jobstate _slurm2icrmstate(enum job_states slurm_state);
static icrmerr_t _slurm_socket(icrm_context_t *icrm);
static icrmerr_t _writerr(icrm_context_t *icrm,
                          const char *filename, int lineno,
                          const char *funcname, const char *format, ...);

struct icrm_context {
  char           errstr[ICC_ERRSTR_LEN];
  int            sock;          /* Slurm communication socket */
  unsigned short port;          /* Slurm communication port */
};


icrmerr_t
icrm_init(icrm_context_t **icrm)
{
  CHECK_NULL(icrm);

  icrmerr_t rc;
  icrm_context_t *new;

  *icrm = NULL;

  new = calloc(1, sizeof(*new));

  if (!new)
    return ICRM_ENOMEM;

  *icrm = new;

  rc = _slurm_socket(*icrm);
  if (rc)
    return ICRM_FAILURE;

  return ICRM_SUCCESS;
}


void
icrm_fini(icrm_context_t **icrm)
{
  if (icrm) {
    if (*icrm) {
      close((*icrm)->sock);
      free(*icrm);
    }
    *icrm = NULL;
  }
}


char *
icrm_errstr(icrm_context_t *icrm)
{
  if (!(icrm && icrm->errstr))
    return NULL;

  return icrm->errstr;
}


icrmerr_t
icrm_jobstate(icrm_context_t *icrm, uint32_t jobid, enum icrm_jobstate *jobstate)
{
  CHECK_ICRM(icrm);
  CHECK_NULL(jobstate);

  icrmerr_t rc;
  int slurmrc;
  job_info_msg_t *buf = NULL;

  rc = ICRM_SUCCESS;

  slurmrc = slurm_load_job(&buf, jobid, SHOW_ALL);

  if (slurmrc != SLURM_SUCCESS) {
    if (errno == ESLURM_INVALID_JOB_ID) {
      rc = ICRM_EJOBID;
    } else{
      rc = ICRM_FAILURE;
    }
    WRITERR(icrm, "slurm: %s", slurm_strerror(errno));
    goto end;
  }

  /* only one job with a given jobid? */
  assert(buf->record_count == 1);

  *jobstate = _slurm2icrmstate(buf->job_array[0].job_state);

 end:
  if (buf) {
    slurm_free_job_info_msg(buf);
  }
  return rc;
}

icrmerr_t
icrm_alloc(struct icrm_context *icrm, char shrink, uint32_t nnodes)
{
  icrmerr_t rc;
  job_desc_msg_t jobreq;
  resource_allocation_response_msg_t *resp;

  rc = ICRM_SUCCESS;

  if (shrink) {
    return ICRM_ENOTIMPL;
  }

  slurm_init_job_desc_msg(&jobreq);
  jobreq.min_nodes = nnodes;
  jobreq.max_nodes = nnodes;
  jobreq.shared = 0;
  jobreq.user_id = getuid();
  jobreq.group_id = getgid();
  /* jobreq.alloc_resp_port = icrm->port; */

  /* wait indefinitely */
  resp = slurm_allocate_resources_blocking(&jobreq, 0, NULL);
  if (resp == NULL) {
    WRITERR(icrm, "slurm_allocate_resources_blocking: %s\n", slurm_strerror(slurm_get_errno()));
    rc = ICRM_ERESOURCEMAN;
  }

  slurm_free_resource_allocation_response_msg(resp);

  return rc;
}


icrmerr_t
icrm_alloc_wait(icrm_context_t *icrm, uint32_t jobid)
{
  icrmerr_t rc;
  struct sockaddr_storage sin;
  socklen_t len = sizeof(sin);
  int sock;

  resource_allocation_response_msg_t *resp;

  sock = accept(icrm->sock, (struct sockaddr *)&sin, &len);
  if (sock == -1) {
    WRITERR(icrm, "accept: %s", strerror(errno));
    rc = ICRM_FAILURE;
  } else {
    /* technically, all the job information has already been sent
       by slurmctld to the socket, but it is unparseable with
       Slurm API, so we resort to a new RPC */
    rc = slurm_allocation_lookup(jobid, &resp);
    if (rc != SLURM_SUCCESS) {
      WRITERR(icrm, "slurm_allocation_lookup: %s", slurm_strerror(slurm_get_errno()));
      rc = ICRM_FAILURE;
    }
  }

  fprintf(stderr, "ALLOCED JOB %u! NODES %s\n", resp->job_id, resp->node_list);
  /* total = 0; */
  /* while ((n = recv(sock, resp + total, sizeof(*resp) - total, 0)) > 0) { */
  /*   total += n; */
  /*   fprintf(stderr, "RECVING\n"); */
  /* } */
  /* if (n == -1) { */
  /*   WRITERR(icrm, "recv: %s", strerror(errno)); */
  /*   rc = ICRM_FAILURE; */
  /* } */
  /* if (total == sizeof(*resp)) { */
  /*   rc = ICRM_SUCCESS; */
  /* } else { */
  /*   WRITERR(icrm, "Incomplete Slurm answer"); */
  /*   rc = ICRM_FAILURE; */
  /* } */
  return ICRM_SUCCESS;
}

static icrmerr_t
_writerr(icrm_context_t *icrm, const char *filename, int lineno,
         const char *funcname, const char *format, ...)
{
  assert(icrm);
  assert(icrm->errstr);

  int nbytes = 0;

  if (filename && lineno && funcname) {
    nbytes = snprintf(icrm->errstr, ICC_ERRSTR_LEN, "%s (%s:%d): ", funcname, filename, lineno);
    if (nbytes < 0) {
      icrm->errstr[0] = '\0';
      return ICRM_FAILURE;
    }
  }

  va_list ap;
  va_start(ap, format);

  if (nbytes < ICC_ERRSTR_LEN) {
    vsnprintf(icrm->errstr + nbytes, ICC_ERRSTR_LEN, format, ap);
  }

  icrm->errstr[ICC_ERRSTR_LEN - 1] = '\0';

  va_end(ap);

  return ICRM_SUCCESS;
}

static icrmerr_t
_slurm_socket(icrm_context_t *icrm)
{
  CHECK_ICRM(icrm);

  struct addrinfo hints, *res, *p;
  int sock, rc;

  icrm->sock = -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV | AI_ADDRCONFIG;

  res = NULL;

  rc = getaddrinfo(NULL, "0", &hints, &res);
  if (rc != 0) {
    WRITERR(icrm, "getaddrinfo: %s", gai_strerror(rc));
    return ICRM_FAILURE;
  }

  for (p = res; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == -1)
      continue;                 /* failure, try next socket */

    if (bind(sock, p->ai_addr, p->ai_addrlen) == 0)
      break;                    /* success */

    close(sock);
  }

  if (p == NULL) {
    WRITERR(icrm, "Slurm socket: Could not bind");
    return ICRM_FAILURE;
  }

  freeaddrinfo(res);

  struct sockaddr_storage sin;
  socklen_t len = sizeof(sin);

  rc = listen(sock, 8);         /* no backlog expected */
  if (rc == -1) {
    WRITERR(icrm, "listen: %s", strerror(errno));
    return ICRM_FAILURE;
  }

  rc = getsockname(sock, (struct sockaddr *)&sin, &len);
  if (rc == -1) {
    WRITERR(icrm, "getsockname: %s", strerror(errno));
    return ICRM_FAILURE;
  }

  icrm->sock = sock;
  if (sin.ss_family == AF_INET) {
    icrm->port = ntohs(((struct sockaddr_in *)&sin)->sin_port);
  } else {
    icrm->port = ntohs(((struct sockaddr_in6 *)&sin)->sin6_port);
  }

  return ICRM_SUCCESS;
}

static enum icrm_jobstate
_slurm2icrmstate(enum job_states slurm_jobstate)
{
  switch (slurm_jobstate) {
  case JOB_PENDING:
    return ICRM_JOB_PENDING;
  case JOB_RUNNING:
    return ICRM_JOB_RUNNING;
  case JOB_SUSPENDED:
  case JOB_COMPLETE:
  case JOB_CANCELLED:
  case JOB_FAILED:
  case JOB_TIMEOUT:
  case JOB_NODE_FAIL:
  case JOB_PREEMPTED:
  case JOB_BOOT_FAIL:
  case JOB_DEADLINE:
  case JOB_OOM:
  case JOB_END:
    break;
  }
  return ICRM_JOB_OTHER;
}
