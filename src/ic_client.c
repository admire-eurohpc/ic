#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "ic.h"


int
main(int argc, char **argv)
{
  int rc;

  struct ic_context *icc;
  ic_init(IC_LOG_INFO, &icc);
  assert(icc != NULL);

  int rpc_retcode;
  char *rpc_retmsg;
  rc = ic_rpc_hello(icc, &rpc_retcode, &rpc_retmsg);
  if (rc == IC_SUCCESS) {
    printf("RPC successful: retcode=%d, \"%s\"\n", rpc_retcode, rpc_retmsg);
    free(rpc_retmsg);
  } else
    fprintf(stderr, "Error making RPC to IC (retcode=%d)\n", rc);

  rc = ic_fini(icc);
  assert(rc == 0);

  return EXIT_SUCCESS;
}
