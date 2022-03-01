#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             /* getopt */

#include "icc.h"


static enum icc_client_type
_icc_typecode(const char *type);


void
usage(void)
{
  (void)fprintf(stderr, "usage: ICC client [--type=mpi|flexmpi|adhoccli|jobmon]\n");
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
  icc_init(ICC_LOG_INFO, typeid, &icc);
  assert(icc != NULL);

  ret = icc_rpc_test(icc, 32, typeid, &rpcret);

  if (ret == ICC_SUCCESS)
    printf("icc_client: RPC \"TEST\" successful: retcode=%d\n", rpcret);
  else
    fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", ret);

  /* FlexMPI apps need to wait for malleability commands */
  if (typeid == ICC_TYPE_FLEXMPI) {
    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_ENTER, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);

    icc_sleep(icc, 4000);

    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_LEAVE, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);
  }

  ret = icc_fini(icc);
  assert(ret == 0);

  return EXIT_SUCCESS;
}


static enum icc_client_type
_icc_typecode(const char *type)
{
  if (!strncmp(type, "mpi", 4)) return ICC_TYPE_MPI;
  else if (!strncmp(type, "flexmpi", 8)) return ICC_TYPE_FLEXMPI;
  else if (!strncmp(type, "adhoccli", 9)) return ICC_TYPE_ADHOCCLI;
  else if (!strncmp(type, "jobmon", 7)) return ICC_TYPE_JOBMON;
  else return ICC_TYPE_UNDEFINED;
}
