#define _GNU_SOURCE             /* for asprintf */
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

#include "hashmap.h"
#include "icc_common.h"
#include "icrm.h"

#define HOSTLIST_BASLEN 128             /* starting len of hostlist */
#define HOSTLIST_MAXLEN 4096            /* reject hostlist that are too long */
#define JOBID_MAXLEN  16                /* 9 is enough for an uint32 */
#define DEPEND_MAXLEN JOBID_MAXLEN + 16 /* enough for dependency string */
#define JOBSTEP_CANCEL_MAXWAIT  60      /* do not wait for jobstep forever */

#define WRITERR(buf,...)  writerr_internal(buf, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define CHECK_NULL(p)  if (!(p)) { return ICRM_EPARAM; }
#define CHECK_ICRM(icrm)  { CHECK_NULL(icrm); CHECK_NULL((icrm)->errstr); }


/**
 * Return info of job JOBID in buffer JOB.
 *
 * The caller is responsible for freeing job.
 */
static icrmerr_t icrm_load_job_internal(uint32_t jobid, job_info_msg_t **job,
                                        char errstr[ICC_ERRSTR_LEN]);

/**
 * Return a hashmap with host:ncpus pairs. Uses the Slurm values HOSTS
 * (node_list), NCPUS (cpus_per_node) and the REPS CPUs repetition
 * count (cpus_count_reps).
 *
 * Return a pointer to the hashmap or NULL in case of memory error.
 *
 * The caller is responsible for freeing the hashmap.
 */
static hm_t *get_hostmap_internal(const char *hosts, uint16_t ncpus[],
                                  uint32_t reps[]);

/**
 * Translate the Slurm job state SLURM_STATE to an icrm_jobstate.
 */
static enum icrm_jobstate slurm2icrmstate(enum job_states slurm_state);

static icrmerr_t writerr_internal(char errstr[ICC_ERRSTR_LEN],
                                  const char *filename, int lineno,
                                  const char *funcname, const char *format, ...);


void
icrm_init(void)
{
  slurm_init(NULL);
}


void
icrm_fini(void)
{
  slurm_fini();
}


icrmerr_t
icrm_jobstate(uint32_t jobid, enum icrm_jobstate *jobstate,
              char errstr[ICC_ERRSTR_LEN])
{
  CHECK_NULL(jobstate);

  icrmerr_t rc;
  job_info_msg_t *buf = NULL;

  rc = icrm_load_job_internal(jobid, &buf, errstr);

  if (rc == ICRM_SUCCESS) {
    *jobstate = slurm2icrmstate(buf->job_array[0].job_state);
  }

  if (buf) {
    slurm_free_job_info_msg(buf);
  }

  return rc;
}


icrmerr_t
icrm_info(uint32_t jobid, uint32_t *ncpus, uint32_t *nnodes, char **nodelist,
          char errstr[ICC_ERRSTR_LEN])
{
  CHECK_NULL(ncpus);
  CHECK_NULL(nnodes);

  icrmerr_t rc;
  job_info_msg_t *buf = NULL;

  *ncpus = 0;
  *nnodes = 0;
  *nodelist = NULL;

  rc = icrm_load_job_internal(jobid, &buf, errstr);
  if (rc != ICRM_SUCCESS) {
    goto end;
  }
  *ncpus = buf->job_array[0].num_cpus;
  *nnodes = buf->job_array[0].num_nodes;

  hostlist_t hl = slurm_hostlist_create(buf->job_array[0].nodes);
  if (!hl) {
    rc = ICRM_FAILURE;
    WRITERR(errstr, "icrm_info: slurm hostlist creation error");
    goto end;
  }

  /* get hosts in a comma separated list
     TODO: not a great use of asprintf, multiple malloc/free */
  char *host;
  while ((host = slurm_hostlist_shift(hl))) {
    char *l;
    int n;
    if (!*nodelist) {
      n = asprintf(&l, "%s", host);
    } else {
      n = asprintf(&l, "%s,%s", *nodelist, host);
      free(*nodelist);
    }
    if (n == -1) {
      rc = ICRM_FAILURE;
      WRITERR(errstr, "icrm_info: node list building error");
      goto end;
    }
    *nodelist = l;
    free(host);
  }
  slurm_hostlist_destroy(hl);


// *nodelist = strdup(buf->job_array[0].nodes);
  // if (!nodelist) {
    // rc = ICRM_ENOMEM;
    // WRITERR(errstr, "icrm_info: nodelist: %s", strerror(errno));
    // goto end;
  // }


end:
  if (buf) {
    slurm_free_job_info_msg(buf);
  }
  return rc;
}


icrmerr_t
icrm_alloc(uint32_t jobid, uint32_t *newjobid, uint32_t *ncpus, hm_t **hostmap,
           char errstr[ICC_ERRSTR_LEN])
{
  icrmerr_t rc;
  int sret;
  unsigned int wait;
  char buf[DEPEND_MAXLEN];
  job_desc_msg_t jobreq;
  resource_allocation_response_msg_t *resp = NULL;
  job_step_info_response_msg_t *stepinfo = NULL;

  rc = ICRM_SUCCESS;

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

  *newjobid = 0;
  *ncpus = 0;

  resp = slurm_allocate_resources_blocking(&jobreq, 0, NULL);

  if (resp == NULL) {
    WRITERR(errstr, "slurm_allocate_resources_blocking: %s",
            slurm_strerror(slurm_get_errno()));
    rc = ICRM_ERESOURCEMAN;
    goto end;
  }

  *newjobid = resp->job_id;

  /* Slurm should always fill these */
  assert(resp->num_cpu_groups >= 1);
  assert(resp->cpus_per_node);
  assert(resp->cpu_count_reps);

  for (uint32_t i = 0; i < resp->num_cpu_groups; i++) {
    *ncpus += resp->cpus_per_node[i] * resp->cpu_count_reps[i];
  }

  *hostmap = get_hostmap_internal(resp->node_list, resp->cpus_per_node,
                                  resp->cpu_count_reps);
  if (*hostmap == NULL) {
    WRITERR(errstr, "Out of memory");
    rc = ICRM_ENOMEM;
    goto end;
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
      WRITERR(errstr, "slurm_terminate_job_step: %s", slurm_strerror(slurm_get_errno()));
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
      WRITERR(errstr, "slurm_get_job_steps: %s", slurm_strerror(slurm_get_errno()));
      goto end;
    }

    if (stepinfo->job_step_count == 0) {
      break;                    /* step has been killed */
    }

    if (wait > 60) {
      rc = ICRM_FAILURE;
      WRITERR(errstr, "Job step did not terminate");
      goto end;
    }
  }

 end:
  if (resp) slurm_free_resource_allocation_response_msg(resp);
  if (stepinfo) slurm_free_job_step_info_response_msg(stepinfo);

  return rc;
}

icrmerr_t
icrm_merge(uint32_t jobid, char errstr[ICC_ERRSTR_LEN])
{
  icrmerr_t rc = ICRM_SUCCESS;

  job_array_resp_msg_t *resp = NULL;
  job_desc_msg_t jobdesc;
  slurm_init_job_desc_msg(&jobdesc);

  /* according to Slurm update_job.c, only min_nodes should be set there */
  jobdesc.min_nodes = 0;
  jobdesc.job_id = jobid;

  int sret = slurm_update_job2(&jobdesc, &resp);
  if (sret != SLURM_SUCCESS) {
    WRITERR(errstr, "slurm_update_job2: %s", slurm_strerror(slurm_get_errno()));
    rc = ICRM_ERESOURCEMAN;
  } else {
    slurm_free_job_array_resp(resp);
  }

  /* XX maybe update Slurm environment? (see Slurm update_job.c)*/

  return rc;
}


icrmerr_t
icrm_release_node(const char *nodename, uint32_t jobid, uint32_t ncpus,
                  char errstr[ICC_ERRSTR_LEN])
{
  assert(jobid);
  assert(nodename);
  assert(ncpus > 0);

  icrmerr_t ret = ICRM_SUCCESS;
  resource_allocation_response_msg_t *allocinfo = NULL;

  int sret = slurm_allocation_lookup(jobid, &allocinfo);
  if (sret != SLURM_SUCCESS) {
    WRITERR(errstr, "slurm_allocation_lookup: %s", slurm_strerror(slurm_get_errno()));
    return ICRM_ERESOURCEMAN;
  }

  hm_t *hostmap = get_hostmap_internal(allocinfo->node_list,
                                       allocinfo->cpus_per_node,
                                       allocinfo->cpu_count_reps);

  slurm_free_resource_allocation_response_msg(allocinfo);

  if (!hostmap) {
    return ICRM_ENOMEM;
  }

  const uint16_t *nalloced = hm_get(hostmap, nodename);
  if (!nalloced || *nalloced != ncpus) {
    WRITERR(errstr, "Cannot release node %s:%"PRIu16", %"PRIu16" CPUs allocated",
            nodename, ncpus, nalloced ? *nalloced : 0);
    if (*nalloced > ncpus) {
      ret = ICRM_EAGAIN;
    } else {
      ret = ICRM_FAILURE;
    }

    hm_free(hostmap);
    return ret;
  }

  /* generate new hostlist with the node removed */
  uint16_t nocpus = 0;
  int rc = hm_set(hostmap, nodename, &nocpus, sizeof(nocpus));
  if (rc == -1) {
    hm_free(hostmap);
    return ICRM_ENOMEM;
  }

  char *newlist = icrm_hostlist(hostmap, 0, NULL);

  hm_free(hostmap);

  if(!newlist) {
    return ICRM_ENOMEM;
  }

  /* update job, equivalent to:
     "scontrol update JobId=$JOBID NodeList=$HOSTLIST" */
  job_array_resp_msg_t *jobresp = NULL;
  job_desc_msg_t jobreq;
  slurm_init_job_desc_msg(&jobreq);

  jobreq.job_id = jobid;
  jobreq.req_nodes = newlist;

  sret = slurm_update_job2(&jobreq, &jobresp);
  if (sret != SLURM_SUCCESS) {
    WRITERR(errstr, "slurm_update_job2: %s", slurm_strerror(slurm_get_errno()));
    ret = ICRM_ERESOURCEMAN;
  } else if (jobresp) {
    slurm_free_job_array_resp(jobresp);
    ret = ICRM_SUCCESS;
  }

  if (newlist) {
    free(newlist);
  }

  return ret;
}


icrmerr_t
icrm_update_hostmap(hm_t *hostmap, hm_t *newalloc)
{
  assert(hostmap);
  assert(newalloc);

  const char *host;
  const uint16_t *nalloc;
  size_t curs = 0;

  while ((curs = hm_next(newalloc, curs, &host, (const void **)&nalloc)) != 0) {
    const uint16_t *ncpus = hm_get(hostmap, host);
    if (ncpus && UINT16_MAX - *ncpus < *nalloc) {         /* would overflow */
      return ICRM_EOVERFLOW;
    }
    uint16_t ntotal = *nalloc + (ncpus ? *ncpus : 0);
    hm_set(hostmap, host, &ntotal, sizeof(ntotal));
  }

  return ICRM_SUCCESS;
}


char *
icrm_hostlist(hm_t *hostmap, char withcpus, uint32_t *ncpus_total)
{
  char *buf, *tmp;
  size_t bufsize, nwritten, n, cursor;
  const char *host;
  const uint16_t *ncpus;

  if (ncpus_total) {
    *ncpus_total = 0;
  }

  bufsize = 512;            /* start with a reasonably sized buffer */

  buf = malloc(bufsize);
  if (!buf) {
    return NULL;
  }
  buf[0] = '\0';

  nwritten = n = 0;
  cursor = 0;

  while ((cursor = hm_next(hostmap, cursor, &host, (const void **)&ncpus)) != 0) {
    assert(ncpus);

    if (ncpus_total) {
      *ncpus_total += *ncpus;
      if (*ncpus_total < *ncpus) { /* overflow */
        *ncpus_total = UINT32_MAX;
        free(buf);
        return NULL;
      }
    }

    if (*ncpus == 0) {          /* ignore node with no CPUs */
      continue;
    } else if (withcpus) {
      n = snprintf(buf + nwritten, bufsize - nwritten, "%s%s:%"PRIu16,
                   nwritten > 0 ? "," : "", host, *ncpus);
    } else {
      n = snprintf(buf + nwritten, bufsize - nwritten, "%s%s",
                   nwritten > 0 ? "," : "", host);
    }

    if (n < bufsize - nwritten) {
      nwritten += n;
    } else {
      tmp = reallocarray(buf, 2, bufsize);
      if (!tmp) {
        free(buf);
        return NULL;
      }
      buf = tmp;
      bufsize *= 2;             /* potential overflow catched by reallocarray */
    }
  }

  return buf;
}

static icrmerr_t
writerr_internal(char buf[ICC_ERRSTR_LEN], const char *filename, int lineno,
                 const char *funcname, const char *format, ...)
{
  int nbytes = 0;

  if (filename && lineno && funcname) {
    nbytes = snprintf(buf, ICC_ERRSTR_LEN, "%s (%s:%d): ", funcname, filename, lineno);
    if (nbytes < 0) {
      buf[0] = '\0';
      return ICRM_FAILURE;
    }
  }

  va_list ap;
  va_start(ap, format);

  if (nbytes < ICC_ERRSTR_LEN) {
    vsnprintf(buf + nbytes, ICC_ERRSTR_LEN, format, ap);
  }

  buf[ICC_ERRSTR_LEN - 1] = '\0';

  va_end(ap);

  return ICRM_SUCCESS;
}

static enum icrm_jobstate
slurm2icrmstate(enum job_states slurm_jobstate)
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
icrm_load_job_internal(uint32_t jobid, job_info_msg_t **job,
                       char errstr[ICC_ERRSTR_LEN])
{
  icrmerr_t rc;
  int slurmrc;

  rc = ICRM_SUCCESS;

  /* show local to avoid Slurm federation lookup */
  slurmrc = slurm_load_job(job, jobid, SHOW_LOCAL);

  if (slurmrc != SLURM_SUCCESS) {
    if (errno == ESLURM_INVALID_JOB_ID) {
      rc = ICRM_EJOBID;
    } else{
      rc = ICRM_FAILURE;
    }
    WRITERR(errstr, "slurm: %s", slurm_strerror(errno));
  }

  return rc;
}

static hm_t *
get_hostmap_internal(const char *hostlist, uint16_t cpus_per_node[],
                     uint32_t cpus_count_reps[])
{
  assert(hostlist);
  assert(cpus_per_node);
  assert(cpus_count_reps);

  hostlist_t hl;
  uint16_t ncpus;
  uint32_t reps;
  size_t icpu;

  hm_t *hostmap = hm_create();
  if (!hostmap) {               /* out of memory */
    return NULL;
  }

  hl = slurm_hostlist_create(hostlist);
  if (!hl) {
    hm_free(hostmap);
    return NULL;
  }

  /* Slurm returns the CPU counts grouped. There is num_cpu_groups
     groups. For each group i cpus_per_node[i] is the CPU count,
     repeated cpus_count_reps[i] time */

  icpu = 0;
  reps = cpus_count_reps[icpu];

  char *host;
  while ((host = slurm_hostlist_shift(hl))) {
    ncpus = cpus_per_node[icpu];
    reps--;
    if (reps == 0)
      icpu++;

    hm_set(hostmap, host, &ncpus, sizeof(ncpus));

    free(host);
  }

  slurm_hostlist_destroy(hl);

  return hostmap;
}
