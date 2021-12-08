/*
 * Application manager is in charge of receiving the FlexMPI instructions from the Intelliget Controller
 * Its runs as a server. IC as a client.
 *
 */


#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             /* getopt */
#include <pthread.h>

#include "icc.h"
#include "icc_rpc.h"

struct icc_context *icc;


void
usage(void)
{
  (void)fprintf(stderr, "usage: app_manager -b\n");
  exit(1);
}


int
main(int argc, char **argv) {
  static int bidir = 0;
  static struct option longopts[] = {
    {"bidirectional", no_argument, &bidir, 1},
    {NULL,            0,           NULL,   0}
  };

  int ch;
  while ((ch = getopt_long(argc, argv, "b", longopts, NULL)) != -1)
    switch (ch) {
      case 'b':
        bidir = 1;
        break;
      case 0:
        continue;
      default:
        usage();
    }
  argc -= optind;
  argv += optind;


  int rc;

  while(1) {
    icc_init(ICC_LOG_INFO, bidir, &icc); //bidir = 1
    assert(icc != NULL);

    /* We send 32, but we expect the answer with the address*/
    /*for(int i = 0; i < 1; i++) {
      int rpc_retcode;
      struct icc_rpc_test_in rpc_in = {.number=i};

      rc = icc_rpc_send(icc, ICC_RPC_TEST, &rpc_in, &rpc_retcode);
      if (rc == ICC_SUCCESS)
        printf("RPC successful: retcode=%d\n", rpc_retcode);
      else
        fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);
    }*/

    rc = icc_fini(icc);
    assert(rc == 0);
  }

  return EXIT_SUCCESS;
}



