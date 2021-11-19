#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <margo.h>
#include <inttypes.h>         /* PRIdxx */
#include <pthread.h>
#include "root_connections.h"
#include "icc_rpc.h"
#include "icdb.h"
#include "icc.h"



/* THREAD FUNCTIONS */

/**
 * Thread routine for Malleability Manager
 */
void
malleability_manager_run(void *mid)
{
  printf("[CONTROL] IC server \n ");
  margo_instance_id *mid_p = (margo_instance_id*)mid;
  int ret = register_rpc(*mid_p, ADM_GETSYSTEMSTATUS);
  printf("ADM_GetSystemStatus working\n");
}

/**
 * Thread routine for Slurm
 */
void
slurm_manager_run(void *mid)
{
  margo_instance_id *mid_p = (margo_instance_id*)mid;
  int ret = register_rpc(*mid_p, ADM_JOBSCHEDULESOLUTION);
  printf("ADM_jopbScheduleSolution working\n");
}

/**
 * Test thread for communication with icc_client
 * Send data to ADM_SUGGESTSCHEDULESOLUTION de Malleability Manager
 */
void
malleability_out_run() {
  while(1) {
    printf("[CONTROL] IC to Malleability Manager\n");
    struct icc_context *icc;
    //icc_init(ICC_LOG_INFO, &icc);
    icc_init_opt(ICC_LOG_INFO, &icc, MALLEABILITY_MARGO_SERVER); // We want to send data to Malleability Manager
    int rpc_retcode;
    struct icc_rpc_malleability_manager_out rpc = {.number=5};
    int rc = icc_rpc_send(icc, ICC_RPC_MALLEABILITY_OUT, &rpc, &rpc_retcode);
    if (rc == ICC_SUCCESS)
      printf("RPC ADM_SUGGESTSCHEDULESOLUTION successful: retcode=%d\n", rpc_retcode);
    else
      fprintf(stderr, "Error sending RPC to Malleability Manager (retcode=%d)\n", rc);
    rc = icc_fini(icc);
    sleep(2);
  }
}


/**
 * Entry point. No arguments
 * @return Negative values in case of error.
 */
int
main(int argc, char ** argv __attribute__((unused))) {

  /*
   * Check input args.
   */
  if (argc != 1) {
    printf("Root Controller does not need parameters.");
    return -1;
  }

  /*
   * Header
   */
  printf("**** ADMIRE ROOT CONTROLLER ****\n");
  printf("Thread 1: Margo server for RCPs\n");
  printf("Thread 2: Malleability Manager\n");
  printf("Thread 3: Slurm\n");
  printf("Thread 4: I/O Scheduler\n");
  printf("Thread 5: Ad-hoc Storage \n");
  printf("Thread 6: Monitoring Manager\n\n");
  printf("Thread 7: System Analytic Component\n");
  printf("Thread 8: Distributed Database Connector\n\n");
  printf("Foreach App_x\n");
  printf("\tThread x+1: Application_x Manager\n");
  printf("\tThread x+2: Application_x Performance collector\n\n");


  /*
   * Initialize the IC environment
   */
  /*margo_instance_id mid;
  int ret = load_environment_controller_opt(&mid, IC_MARGO_SERVER);
  if (ret == EXIT_FAILURE) return EXIT_FAILURE;
  printf("Setting up the environment\n");*/

  /*
   * Run threads:
   * 1- RPC for GetSystemStatus communications
   * 2- RPC for Schedule solutions
   * 3- Sending data to Malleability manager
   */
  /*pthread_t malleabMan;
  pthread_create(&malleabMan, NULL, (void *)malleability_manager_run, &mid);*/

  /*pthread_t slurm;
  pthread_create(&slurm, NULL, (void *)slurm_manager_run, &mid);*/

  pthread_t malleability_out;
  pthread_create(&malleability_out, NULL, (void *)malleability_out_run, NULL);


  /* Thread join*/
  //pthread_join(malleabMan, NULL);
  /*pthread_join(slurm, NULL);*/
  pthread_join(malleability_out, NULL);

  //margo_wait_for_finalize(mid);

  return EXIT_SUCCESS;
}


