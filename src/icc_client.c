#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <margo.h>
#include <pthread.h>
#include "root_connections.h"
#include "icc.h"


void
thread_server_run()
{
  printf("[CONTROL] Malleability Manager Server\n ");
  margo_instance_id mid;
  int ret = load_environment_controller_opt(&mid, MALLEABILITY_MARGO_SERVER);
  if (ret == EXIT_FAILURE)
    printf("Error");
  ret = register_rpc(mid, ADM_SUGGESTSCHEDULESOLUTION);
  margo_wait_for_finalize(mid);
}


void
thread_sender_run() {
  /* SEND INFORMATION FROM IC --> Register only the certain RCP: GetSystemStatus*/
  while(1) {
    printf("[CONTROL] Malleability Manager to IC\n");
    struct icc_context *icc;
    icc_init_opt(ICC_LOG_INFO, &icc, IC_MARGO_SERVER); //We want to send data to IC
    int rpc_retcode;
    struct icc_rpc_malleability_manager_in rpc_in = {.number=4};
    int rc = icc_rpc_send(icc, ICC_RPC_MALLEABILITY_IN, &rpc_in, &rpc_retcode);
    if (rc == ICC_SUCCESS)
      printf("RPC GetSystemStatus successful: retcode=%d\n", rpc_retcode);
    else
      fprintf(stderr, "Error sending RPC to IC (retcode=%d)\n", rc);
    rc = icc_fini(icc);
    sleep(2);
  }
}




int main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {

  pthread_t th_server;
  pthread_create(&th_server, NULL, (void *)thread_server_run, NULL);
  //sleep(2);

  /*pthread_t th_sender;
  pthread_create(&th_sender, NULL, (void *)thread_sender_run, NULL);*/

  pthread_join(th_server, NULL);
  //pthread_join(th_sender, NULL);

  return EXIT_SUCCESS;
}
