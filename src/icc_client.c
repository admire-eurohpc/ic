#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "icc.h"


int
main(int argc, char **argv)
{
  int rc;

  struct icc_context *icc;
  icc_init(ICC_LOG_INFO, &icc);
  assert(icc != NULL);

  int rpc_retcode;
  char *rpc_retmsg;
  rc = icc_rpc_hello(icc, &rpc_retcode, &rpc_retmsg);
  if (rc == ICC_SUCCESS) {
    printf("RPC successful: retcode=%d, \"%s\"\n", rpc_retcode, rpc_retmsg);
    free(rpc_retmsg);
  } else
    fprintf(stderr, "Error making RPC to IC (retcode=%d)\n", rc);

  rc = icc_fini(icc);
  assert(rc == 0);

  return EXIT_SUCCESS;
}
