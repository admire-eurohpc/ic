#include <assert.h>
#include <netdb.h>              /* socket */
#include <signal.h>             /* SIG */
#include <stdarg.h>             /* va_ stuff */
#include <stdlib.h>             /* calloc */
#include <stdint.h>             /* uintXX_t */
#include <stdio.h>              /* printf */
#include <string.h>             /* strncpy, strnlen */
#include <unistd.h>             /* sleep */
#include <sys/types.h>          /* socket */
#include <sys/socket.h>         /* socket */

#include <abt.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "icc_common.h"
#include "icrm.h"

#define HOSTLIST_BASLEN 128             /* starting len of hostlist */
#define HOSTLIST_MAXLEN 4096            /* reject hostlist that are too long */
#define JOBID_MAXLEN  16                /* 9 is enough for an uint32 */
#define DEPEND_MAXLEN JOBID_MAXLEN + 16 /* enough for dependency string */
#define JOBSTEP_CANCEL_MAXWAIT  60      /* do not wait for jobstep forever */

#define CHECK_NULL(p)  if (!(p)) { return ICRM_EPARAM; }
#define CHECK_ICRM(icrm)  { CHECK_NULL(icrm); CHECK_NULL((icrm)->errstr); }

#define WRITERR(icrm,...)  _writerr(icrm, __FILE__, __LINE__, __func__, __VA_ARGS__)


/**
 * Return info of job JOBID in buffer JOB.
 *
 * The caller is responsible for freeing job.
 */
static icrmerr_t _icrm_load_job(icrm_context_t *icrm, uint32_t jobid,
                                job_info_msg_t **job);

/**
 * Format HOSTLIST as returned by Slurm into a comma-separated list of
 * host:ncpus. Uses the Slurm values CPUS_PER_NODE, the
 * CPUS_COUNT_REPS repetition count.
 *
 * Return the formatted hostlist or NULL in case of error. The caller
 * is responsible for freeing the hostlist.
 */
static char *_format_slurm_hostlist(const char *hostlist,
                                    uint16_t *cpus_per_node,
                                    uint32_t *cpus_count_reps);

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
  job_info_msg_t *buf = NULL;

  rc = _icrm_load_job(icrm, jobid, &buf);

  if (rc == ICRM_SUCCESS) {
    *jobstate = _slurm2icrmstate(buf->job_array[0].job_state);
  }

  if (buf) {
    slurm_free_job_info_msg(buf);
  }

  return rc;
}


icrmerr_t
icrm_ncpus(icrm_context_t *icrm, uint32_t jobid,
           uint32_t *ncpus, uint32_t *nnodes)
{
  CHECK_ICRM(icrm);
  CHECK_NULL(ncpus);
  CHECK_NULL(nnodes);

  icrmerr_t rc;
  job_info_msg_t *buf = NULL;

  *ncpus = 0;
  *nnodes = 0;

  rc = _icrm_load_job(icrm, jobid, &buf);
  if (rc == ICRM_SUCCESS) {
    *ncpus = buf->job_array[0].num_cpus;
    *nnodes = buf->job_array[0].num_nodes;
  }

  if (buf) {
    slurm_free_job_info_msg(buf);
  }

  return rc;
}


icrmerr_t
icrm_alloc(struct icrm_context *icrm, uint32_t jobid, char shrink,
           uint32_t *ncpus, char **hostlist)
{
  icrmerr_t rc;
  int sret;
  unsigned int wait;
  size_t len;
  char buf[DEPEND_MAXLEN];
  job_desc_msg_t jobreq, jobreq2;
  resource_allocation_response_msg_t *resp;
  job_array_resp_msg_t *resp2;
  job_step_info_response_msg_t *stepinfo;

  assert(hostlist);

  if (shrink) {
    return ICRM_ENOTIMPL;
  }

  rc = ICRM_SUCCESS;
  resp = NULL;
  resp2 = NULL;

  slurm_init_job_desc_msg(&jobreq);

  /* 1. get allocation, equivalent to:
     "salloc -n$ncpus --dependency=expand:$jobid"
     wait indefinitely */
  jobreq.min_cpus = *ncpus;
  jobreq.max_cpus = *ncpus;
  jobreq.shared = 0;
  jobreq.user_id = getuid();    /* necessary on PlaFRIM... */
  jobreq.group_id = getgid();   /* idem */

  snprintf(buf, DEPEND_MAXLEN, "expand:%"PRIu32, jobid);
  jobreq.dependency = buf;

  *ncpus = 0;
  *hostlist = NULL;

  resp = slurm_allocate_resources_blocking(&jobreq, 0, NULL);

  if (resp == NULL) {
    WRITERR(icrm, "slurm_allocate_resources_blocking: %s",
            slurm_strerror(slurm_get_errno()));
    rc = ICRM_ERESOURCEMAN;
    goto end;
  }

  /* Slurm should always fill these */
  assert(resp->num_cpu_groups >= 1);
  assert(resp->cpus_per_node);
  assert(resp->cpu_count_reps);

  /* Slurm returns the CPU counts grouped. There is num_cpu_groups
     groups. For each group i cpus_per_node[i] is the CPU count,
     repeated cpus_count_reps[i] time */

  len = strnlen(resp->node_list, HOSTLIST_MAXLEN);
  if (len == HOSTLIST_MAXLEN) {
    rc = ICRM_ERESOURCEMAN;
    WRITERR(icrm, "slurm_allocate_resources_blocking: hostlist too long");
    goto end;
  }

  *hostlist = _format_slurm_hostlist(resp->node_list,
                                     resp->cpus_per_node, resp->cpu_count_reps);
  if (!*hostlist) {
    rc = ICRM_ERESOURCEMAN;
    WRITERR(icrm, "Could not format Slurm hostlist");
    goto end;
  }

  for (uint32_t i = 0; i < resp->num_cpu_groups; i++) {
    *ncpus += resp->cpus_per_node[i] * resp->cpu_count_reps[i];
  }

  /* this pause makes the upcoming kill much faster than sending the
     signal immediately for some reason */
  sleep(1);

  /* 2. kill external process container .extern. It is a job step that
     is launched automatically by Slurm if PrologFlags=Contain (see
     slurm.conf(5)). The kill procedure is copied from Slurm scancel.c */

  for (int i = 0; i < 10; i++) {
    sret = slurm_kill_job_step(resp->job_id, SLURM_EXTERN_CONT, SIGKILL);

    if (sret == SLURM_SUCCESS || ((errno != ESLURM_TRANSITION_STATE_NO_UPDATE) &&
                                  (errno != ESLURM_JOB_PENDING))) {
      break;
    }
    sleep(5 + i);
  }

  if (sret != SLURM_SUCCESS) {
    sret = slurm_get_errno();
    /* invalid job step = no .extern step, ignore error  */
    if (sret != ESLURM_ALREADY_DONE && sret != ESLURM_INVALID_JOB_ID) {
      WRITERR(icrm, "slurm_terminate_job_step: %s", slurm_strerror(slurm_get_errno()));
      rc = ICRM_ERESOURCEMAN;
      goto end;
    }
  }

  /* wait for the jobstep to finish (no more than JOBSTEP_CANCEL_MAXWAIT) */
  /* XX fixme: this can be avoided for jobs that don’t have any step */
  stepinfo = NULL;
  wait = 1;

  while (1) {
    sleep(wait);
    wait *= 2;

    sret = slurm_get_job_steps(0, resp->job_id, NO_VAL, &stepinfo, SHOW_ALL);
    if (sret != SLURM_SUCCESS) {
      rc = ICRM_FAILURE;
      WRITERR(icrm, "slurm_get_job_steps: %s", slurm_strerror(errno));
      goto end;
    }

    if (stepinfo->job_step_count == 0) {
      break;                    /* step has been killed */
    }

    if (wait > 60) {
      rc = ICRM_FAILURE;
      WRITERR(icrm, "Job step did not terminate");
      goto end;
    }
  }

  /* 2. update allocation, equivalent to:
     "scontrol update JobId=$JOBID NumNodes=0" */
  slurm_init_job_desc_msg(&jobreq2);

  /* according to Slurm update_job.c, for this only min_nodes should
     be set */
  jobreq2.min_nodes = 0;
  jobreq2.job_id = resp->job_id;

  sret = slurm_update_job2(&jobreq2, &resp2);

  if (sret != SLURM_SUCCESS) {
    WRITERR(icrm, "slurm_update_job2: %s", slurm_strerror(slurm_get_errno()));
    rc = ICRM_ERESOURCEMAN;
    goto end;
  }

  /* XX maybe update Slurm environment? (see Slurm update_job.c)*/

 end:
  if (resp) slurm_free_resource_allocation_response_msg(resp);
  if (resp2) slurm_free_job_array_resp(resp2);
  if (stepinfo) slurm_free_job_step_info_response_msg(stepinfo);

  return rc;
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

static icrmerr_t
_icrm_load_job(icrm_context_t *icrm, uint32_t jobid, job_info_msg_t **job)
{
  CHECK_ICRM(icrm);

  icrmerr_t rc;
  int slurmrc;

  rc = ICRM_SUCCESS;

  slurmrc = slurm_load_job(job, jobid, SHOW_ALL);

  if (slurmrc != SLURM_SUCCESS) {
    if (errno == ESLURM_INVALID_JOB_ID) {
      rc = ICRM_EJOBID;
    } else{
      rc = ICRM_FAILURE;
    }
    WRITERR(icrm, "slurm: %s", slurm_strerror(errno));
  }

  return rc;
}

static char *
_format_slurm_hostlist(const char *hostlist,
                       uint16_t *cpus_per_node, uint32_t *cpus_count_reps)
{
  assert(hostlist);
  assert(cpus_per_node);
  assert(cpus_count_reps);

  hostlist_t hl;
  uint16_t ncpus;
  uint32_t reps;
  char *host, *res, *tmp;
  size_t len, ntotal, n, icpu;

  len = HOSTLIST_BASLEN;

  res = malloc(len);
  if (!res)
    return NULL;

  hl = slurm_hostlist_create(hostlist);
  if (!hl) {
    return NULL;
  }

  ntotal = n = 0;
  icpu = 0;
  reps = cpus_count_reps[icpu];

  while ((host = slurm_hostlist_shift(hl))) {
    ncpus = cpus_per_node[icpu];
    reps--;
    if (reps == 0)
      icpu++;

    n = snprintf(NULL, 0, "%s:%"PRIu16",", host, ncpus);

    while (ntotal + n >= len) {
      len *= 2;
      tmp = realloc(res, len);
      if (!tmp) {
        free(res);
        break;
      }
      res = tmp;
    }
    if (!res) {
      free(host);
      break;
    }

    sprintf(res + ntotal, "%s:%"PRIu16",", host, ncpus);
    ntotal += n;
    free(host);
  }

  /* remove last comma */
  if (res)
    res[ntotal - 1] = '\0';

  slurm_hostlist_destroy(hl);

  return res;
}
