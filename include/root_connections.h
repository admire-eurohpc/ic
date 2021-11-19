//
// Created by Alberto Cascajo on 15/11/21.
// This class contains the structs and functions related to the connections with the root controller
//

#ifndef RC_ROOT_CONTROLLER_H
#define RC_ROOT_CONTROLLER_H

/* Include ICC files */
#include <margo.h>


/*
 * Each component will have its own margo_server.
 * These constants provide the FLAG to initialize the appropriate server for each one.
 * 0 -> Initializes the margo server in IC component
 * 1 -> Initializes the margo server in Malleability Manager component
 * 2 -> Initializes the margo server in Slurm Manager component
 * 3 -> Initializes the margo server in I/O Scheduler component
 * 4 -> Initializes the margo server in AdHoc storage component
 * 5 -> Initializes the margo server in Monitoring Manager component
 * 6 -> Initializes the margo server in Application Manager component
 */
#define IC_MARGO_SERVER             0
#define MALLEABILITY_MARGO_SERVER   1
#define SLURM_MARGO_SERVER          2
#define IO_SCHEDULER_MARGO_SERVER   3
#define ADHOC_STORAGE_MARGO_SERVER  4
#define MONITORING_MARGO_SERVER     5
#define APP_MANAGER_MARGO_SERVER    6


/*
 * Constants to access the correct file related to a certain margo server.
 * The order corresponds to the same as the constants to access to the different margo servers.
 */
#define FILE_IC_MARGO_SERVER              "/home/maestro/icc.addr0"
#define FILE_MALLEABILITY_MARGO_SERVER    "/home/maestro/icc.addr1"
#define FILE_SLURM_MARGO_SERVER           "/home/maestro/icc.addr2"
#define FILE_IO_SCHEDULER_MARGO_SERVER    "/home/maestro/icc.addr3"
#define FILE_ADHOC_STORAGE_MARGO_SERVER   "/home/maestro/icc.addr4"
#define FILE_MONITORING_MARGO_SERVER      "/home/maestro/icc.addr5"
#define FILE_APP_MANAGER_MARGO_SERVER     "/home/maestro/icc.addr6"


/*
 * Index of the RPCs that can be registered by rpc_register().
 * The code allows to have a generic and shared function, which creates
 * the RPCs based on their identifier in the ADMIRE documentation.
 * It should be completed
 */
#define ADM_GETSYSTEMSTATUS       0
#define ADM_JOBSCHEDULESOLUTION   1
#define ADM_IOSCHEDULESOLUTION    2
#define ADM_IOSTATUS              3
#define ADM_SLURMSTATE            4
#define ADM_SUGGESTSCHEDULESOLUTION 5


/*
 * Function interfaces for creating the connections
 * */
int load_environment_controller(margo_instance_id);
int load_environment_controller_opt(margo_instance_id*, int);
int register_rpc(margo_instance_id, int);




#endif //RC_ROOT_CONTROLLER_H
