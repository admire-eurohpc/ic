#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* exit */
#include <string.h>             /* strerror */
#include <unistd.h>             /* getopt */

#include "icc.h"

static int strtouint32(const char *nptr, uint32_t *dest);


void
usage(void)
{
  fputs("usage: jobcleaner [--jobid=<jobid>]\n"
        "The jobid can also be picked up in a Slurm environment variable\n", stderr);
  exit(1);
}


int
main(int argc, char **argv)
{
  static struct option longopts[] = {
    { "jobid", required_argument, NULL, 'j' },
    { NULL,    0,                 NULL,  0  },
  };

  int ch, ret, rpcret;
  uint32_t jobid;
  char *jobidstr;

  jobid = 0;
  jobidstr = NULL;

  while ((ch = getopt_long(argc, argv, "j:", longopts, NULL)) != -1)
    switch (ch) {
    case 'j':
      jobidstr = optarg;
    case 0:
      continue;
    default:
      usage();
    }
  argc -= optind;
  argv += optind;


  /* jobid not passed as argument, look in env */
  if (!jobidstr) {
    jobidstr = getenv("SLURM_JOB_ID");
    if (!jobidstr) {
      jobidstr = getenv("SLURM_JOBID");
    }
  }

  if (!jobidstr) {
    usage();
  }

  ret = strtouint32(jobidstr, &jobid);
  if (ret) {
    fprintf(stderr, "Error converting job ID to integer: %s\n", strerror(-ret));
    exit(1);
  }

  struct icc_context *icc;
  icc_init(ICC_LOG_INFO, ICC_TYPE_JOBCLEANER, 0, &icc);
  assert(icc != NULL);

  ret = icc_rpc_jobclean(icc, jobid, &rpcret);
  assert(ret == ICC_SUCCESS);
  assert(rpcret == ICC_SUCCESS);

  ret = icc_fini(icc);
  assert(ret == 0);

  return EXIT_SUCCESS;
}


static int
strtouint32(const char *nptr, uint32_t *dest)
{
  char *end;
  unsigned long long val;

  assert(nptr != NULL);
  assert(dest != NULL);

  *dest = 0;
  errno = 0;

  val = strtoull(nptr, &end, 0);

  if (errno != 0) {
    return -errno;
  }
  else if (end == nptr || (*end != '\0' && *end != '\n')) {
    return -EINVAL;
  }

  if (val > UINT32_MAX)
    return -EINVAL;

  *dest = val;

  return 0;
}
