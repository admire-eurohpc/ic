#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             /* getopt */

#include "icc.h"


int reconfig(int shrink, uint32_t maxprocs, const char *hostlist, void *data);

static enum icc_client_type
_icc_typecode(const char *type);


void
usage(void)
{
  (void)fprintf(stderr, "usage: ICC client [--type=mpi|flexmpi|adhoccli|jobmon|iosets]\n");
  exit(1);
}


int
main(int argc, char **argv)
{
  static struct option longopts[] = {
    { "type", required_argument, NULL, 't' },
    { NULL,   0,                 NULL,  0  },
  };

  int ch;
  enum icc_client_type typeid = 0;

  while ((ch = getopt_long(argc, argv, "t:", longopts, NULL)) != -1)
    switch (ch) {
    case 't':
      typeid = _icc_typecode(optarg);
      if (typeid == ICC_TYPE_UNDEFINED)
        usage();
      break;
    case 0:
      continue;
    default:
      usage();
    }
  argc -= optind;
  argv += optind;

  int ret;
  int rpcret;

  struct icc_context *icc;
  icc_init(ICC_LOG_DEBUG, typeid, &icc);
  assert(icc != NULL);

  if (typeid != ICC_TYPE_IOSETS) {
    ret = icc_rpc_test(icc, 32, typeid, &rpcret);
    if (ret == ICC_SUCCESS)
      printf("icc_client: RPC \"TEST\" successful: retcode=%d\n", rpcret);
    else
      fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", ret);
  }

  /* FlexMPI apps need to wait for malleability commands */
  if (typeid == ICC_TYPE_FLEXMPI) {
    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_ENTER, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);

    icc_sleep(icc, 4000);

    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_LEAVE, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);
  }
  else if (typeid == ICC_TYPE_MPI) {
    /* wait for allocation request */
    icc_sleep(icc, 2000);
  }
  else if (typeid == ICC_TYPE_IOSETS) {
    fputs("[IO-sets] io_begin\n", stderr); /* write to stderr to avoid buffering */
    ret = icc_hint_io_begin(icc);
    assert(ret == ICC_SUCCESS);

    icc_sleep(icc, 8000);

    fputs("[IO-sets] io_end\n", stderr);
    ret = icc_hint_io_end(icc);
    assert(ret == ICC_SUCCESS);
  }

  puts("icc_client: Finishing");
  ret = icc_fini(icc);
  assert(ret == 0);

  return EXIT_SUCCESS;
}

int
reconfig(int shrink, uint32_t maxprocs, const char *hostlist,
         void *data __attribute__((unused))) {
  fprintf(stdout, "IN RECONFIG: %s%d processes on %s\n",
          shrink ? "-" : "", maxprocs, hostlist);
  return 0;
}

static enum icc_client_type
_icc_typecode(const char *type)
{
  if (!strncmp(type, "mpi", 4)) return ICC_TYPE_MPI;
  else if (!strncmp(type, "flexmpi", 8)) return ICC_TYPE_FLEXMPI;
  else if (!strncmp(type, "adhoccli", 9)) return ICC_TYPE_ADHOCCLI;
  else if (!strncmp(type, "jobmon", 7)) return ICC_TYPE_JOBMON;
  else if (!strncmp(type, "iosets", 7)) return ICC_TYPE_IOSETS;
  else return ICC_TYPE_UNDEFINED;
}
