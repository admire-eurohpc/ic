#include <iostream>
#include <margo.h>
#include <inttypes.h>         /* PRIdxx */
#include <thread>
#include "../include/root_connections.h"

/* Include ICC files */
#include "../../include/icc_rpc.h"
#include "../../include/icdb.h"



/* STD namespace global */
using namespace std;



/* THREAD FUNCTIONS */

/**
 * Thread routine for Malleability Manager
 */
void MalleabilityManagerRun(margo_instance_id mid) {
    int ret = RegisterRPC(mid, 0);
    cout << "Malleability Manager working\n" << endl;
}

/**
 * Thread routine for Slurm
 */
void SlurmManagerRun(margo_instance_id mid) {
    int ret = RegisterRPC(mid, 1);
    cout << "Slurm Manager working\n" << endl;
}

/**
 * Thread routine for I/O scheduler
 */
void IOSchedulerRun(margo_instance_id mid) {
    int ret = RegisterRPC(mid, 2);
    cout << "I/O Scheduler working\n" << endl;
}

/**
 * Thread routine for Adhoc storage
 */
void AdhocStorageManagerRun(margo_instance_id mid) {
    int ret = RegisterRPC(mid, 3);
    cout << "Adhoc storage working\n" << endl;
}

/**
 * Thread routine for Monitoring Manager
 */
void MonitoringManagerRun(margo_instance_id mid) {
    int ret = RegisterRPC(mid, 4);
    cout << "Monitoring Manager working\n" << endl;
}





/**
 * Entry point. No arguments
 * @return Negative values in case of error.
 */
int main(int argc, char ** argv) {

    /*
     * Check input args.
     */
    if(argc != 1){
        cout << "Root Controller does not need parameters." << endl;
        return -1;
    }

    /*
     * Header
     */
    cout << "**** ADMIRE ROOT CONTROLLER ****" << endl << endl;
    unsigned int max_concurrency = thread::hardware_concurrency(); //gets the max concurrency.
    cout << "Max concurrency: " << max_concurrency << endl;
    cout << "Thread 1: Margo server for RCPs" << endl;
    cout << "Thread 2: Malleability Manager" << endl;
    cout << "Thread 3: Slurm" << endl;
    cout << "Thread 4: I/O Scheduler" << endl;
    cout << "Thread 5: Ad-hoc Storage " << endl;
    cout << "Thread 6: Monitoring Manager" << endl;
    cout << endl;
    cout << "Thread 7: System Analytic Component" << endl;
    cout << "Thread 8: Distributed Database Connector" << endl;
    cout << endl;
    cout << "Foreach App_x" << endl;
    cout << "\tThread x+1: Application_x Manager" << endl;
    cout << "\tThread x+2: Application_x Performance collector" << endl << endl;


    /*
     * Check the environment
     */
    margo_instance_id mid;
    int ret = LoadEnvironmentController(mid);
    if(ret == EXIT_FAILURE) return EXIT_FAILURE;
    cout << "Setting up the environment" << endl;


    /*
     * Run threads
     */

    /*Malleability manager thread*/
    thread malleabMan (MalleabilityManagerRun, mid);

    /*Malleability manager thread*/
    thread slurm (SlurmManagerRun, mid);



    /* Thread join*/
    malleabMan.join();
    slurm.join();
    /*iosched.join();
    adhocMan.join();
    monitMan.join();*/


    /*Finish Margo*/
    margo_wait_for_finalize(mid);

    return EXIT_SUCCESS;
}


