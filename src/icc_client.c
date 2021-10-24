#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "icc.h"


int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
  int rc;

  struct icc_context *icc;
  icc_init(ICC_LOG_INFO, &icc);
  assert(icc != NULL);

  int rpc_retcode;
  struct icc_rpc_test_in rpc_in = { .number=32 };

  rc = icc_rpc_send(icc, ICC_RPC_TEST, &rpc_in, &rpc_retcode);
  if (rc == ICC_SUCCESS)
    printf("RPC successful: retcode=%d\n", rpc_retcode);
  else
    fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);

  rc = icc_fini(icc);
  assert(rc == 0);

  return EXIT_SUCCESS;
}
