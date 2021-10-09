#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "ic.h"


int
main(int argc, char** argv)
{
  int rc;
  struct ic_context *icc = ic_init(IC_LOG_INFO);
  assert(icc != NULL);

  rc = ic_make_rpc(icc);
  assert(rc == 0);

  rc = ic_fini(icc);
  assert(rc == 0);

  return EXIT_SUCCESS;
}
