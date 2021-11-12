#include <iostream>
#include <margo.h>
#include <inttypes.h>         /* PRIdxx */
#include <thread>

/* Include ICC files */
#include "../../include/icc_rpc.h"
#include "../../include/icdb.h"


/*Function interfaces*/
int LoadEnvironmentController(margo_instance_id&);
int RegisterRPC(margo_instance_id&, int);


/*RPCs for Root controller*/
static void icc_malleab_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_malleab_cb)
static void icc_slurm_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_slurm_cb)
static void icc_iosched_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_iosched_cb)
static void icc_adhoc_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_adhoc_cb)
static void icc_monitor_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_monitor_cb)


/* STD namespace global */
using namespace std;


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
 * It is the same as LoadEnvironmentController but for threads (UNUSED)
  * @param option From 0 to 4, register each RPC in Margo (MM, Slurm, IO, AdH, Mon)
  */
void LoadEnvironmentControllerThread(int option){
    margo_instance_id mid;
    mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
    if (!mid) {
        margo_error(mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
         exit(EXIT_FAILURE);
    }
    margo_set_log_level(mid, MARGO_LOG_INFO);

    if(margo_is_listening(mid) == HG_FALSE) {
        margo_error(mid, "Margo instance is not a server");
        margo_finalize(mid);
        exit(EXIT_FAILURE);
    }

    hg_return_t hret;
    hg_addr_t addr;
    hg_size_t addr_str_size = ICC_ADDR_MAX_SIZE;
    char addr_str[ICC_ADDR_MAX_SIZE];

    hret = margo_addr_self(mid, &addr);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not get Margo self address");
        margo_finalize(mid);
        exit(EXIT_FAILURE);
    }

    hret = margo_addr_to_string(mid, addr_str, &addr_str_size, addr);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not convert Margo self address to string");
        margo_addr_free(mid, addr);
        margo_finalize(mid);
        exit(EXIT_FAILURE);
    }

    hret = margo_addr_free(mid, addr);
    if (hret != HG_SUCCESS)
        margo_error(mid, "Could not free Margo address");

    margo_info(mid, "Margo Server running at address %s", addr_str);

    char *path = icc_addr_file();
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        margo_error(mid, "Could not open address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
        free(path);
        margo_finalize(mid);
        exit(EXIT_FAILURE);
    }
    free(path);

    int nbytes = fprintf(f, "%s", addr_str);
    if (nbytes < 0 || (unsigned)nbytes != addr_str_size - 1) {
        margo_error(mid, "Error writing to address file", strerror(errno));
        fclose(f);
        margo_finalize(mid);
        exit(EXIT_FAILURE);
    }
    fclose(f);


    /* Filter by option (each thread executes one RPC registration */
    if(option == 0) {
        /*RPC MALLEABILITY INPUT*/
        hg_id_t rpc_malleab_in;
        hg_bool_t flag_mall;
        string malleab_name = "icc_malleabMan";
        margo_provider_registered_name(mid, malleab_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_malleab_in,
                                       &flag_mall);
        if (flag_mall == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            exit(EXIT_FAILURE);
        }

        rpc_malleab_in = MARGO_REGISTER_PROVIDER(mid, malleab_name.c_str(),
                                                 malleabilityman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_malleab_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_malleab_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 1) {

        /*RPC SLURM INPUT*/
        hg_id_t rpc_slurm_in;
        hg_bool_t flag_slurm;
        string slurm_name = "icc_slurmMan";
        margo_provider_registered_name(mid, slurm_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_slurm_in,
                                       &flag_slurm);
        if (flag_slurm == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            exit(EXIT_FAILURE);
        }

        rpc_slurm_in = MARGO_REGISTER_PROVIDER(mid, slurm_name.c_str(),
                                               slurmman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                               rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                               icc_slurm_cb, /* HANDLER CODED BELOW */
                                               ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                               ABT_POOL_NULL);

        (void) rpc_slurm_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 2) {

        /*RPC IOSCHED INPUT*/
        hg_id_t rpc_iosched_in;
        hg_bool_t flag_iosched;
        string iosched_name = "icc_iosched";
        margo_provider_registered_name(mid, iosched_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_iosched_in,
                                       &flag_iosched);
        if (flag_iosched == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            exit(EXIT_FAILURE);
        }

        rpc_iosched_in = MARGO_REGISTER_PROVIDER(mid, iosched_name.c_str(),
                                                 iosched_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_iosched_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_iosched_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 3) {

        /*RPC ADHOC INPUT*/
        hg_id_t rpc_adhoc_in;
        hg_bool_t flag_adhoc;
        string adhoc_name = "icc_adhocMan";
        margo_provider_registered_name(mid, adhoc_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_adhoc_in,
                                       &flag_adhoc);
        if (flag_adhoc == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            exit(EXIT_FAILURE);
        }

        rpc_adhoc_in = MARGO_REGISTER_PROVIDER(mid, adhoc_name.c_str(),
                                               adhocman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                               rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                               icc_adhoc_cb, /* HANDLER CODED BELOW */
                                               ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                               ABT_POOL_NULL);

        (void) rpc_adhoc_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 4) {

        /*RPC MONITOR INPUT*/
        hg_id_t rpc_monitor_in;
        hg_bool_t flag_mon;
        string mon_name = "icc_monitorMan";
        margo_provider_registered_name(mid, mon_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_monitor_in,
                                       &flag_mon);
        if (flag_mon == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            exit(EXIT_FAILURE);
        }

        rpc_monitor_in = MARGO_REGISTER_PROVIDER(mid, mon_name.c_str(),
                                                 monitor_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_monitor_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_monitor_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else {
        cerr << "Option " << option << " for RPC registration is not valid." << endl;
        exit(EXIT_FAILURE);
    }



    /* register other RPCs here */


    margo_wait_for_finalize(mid);

    exit(EXIT_SUCCESS);
}


/**
 * Register the RPC (based on option param) in Margo and run the environment
  * @param option From 0 to 4, register each RPC in Margo (MM, Slurm, IO, AdH, Mon)
  * @return 0 success, -1 failure
  */
int LoadEnvironmentController(margo_instance_id& mid){

    mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
    if (!mid) {
        margo_error(mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
        return (EXIT_FAILURE);
    }
    margo_set_log_level(mid, MARGO_LOG_INFO);

    if(margo_is_listening(mid) == HG_FALSE) {
        margo_error(mid, "Margo instance is not a server");
        margo_finalize(mid);
        return (EXIT_FAILURE);
    }

    hg_return_t hret;
    hg_addr_t addr;
    hg_size_t addr_str_size = ICC_ADDR_MAX_SIZE;
    char addr_str[ICC_ADDR_MAX_SIZE];

    hret = margo_addr_self(mid, &addr);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not get Margo self address");
        margo_finalize(mid);
        return (EXIT_FAILURE);
    }

    hret = margo_addr_to_string(mid, addr_str, &addr_str_size, addr);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not convert Margo self address to string");
        margo_addr_free(mid, addr);
        margo_finalize(mid);
        return (EXIT_FAILURE);
    }

    hret = margo_addr_free(mid, addr);
    if (hret != HG_SUCCESS)
        margo_error(mid, "Could not free Margo address");

    margo_info(mid, "Margo Server running at address %s", addr_str);

    char *path = icc_addr_file();
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        margo_error(mid, "Could not open address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
        free(path);
        margo_finalize(mid);
        return (EXIT_FAILURE);
    }
    free(path);

    int nbytes = fprintf(f, "%s", addr_str);
    if (nbytes < 0 || (unsigned)nbytes != addr_str_size - 1) {
        margo_error(mid, "Error writing to address file", strerror(errno));
        fclose(f);
        margo_finalize(mid);
        return (EXIT_FAILURE);
    }
    fclose(f);

    return (EXIT_SUCCESS);
}


/**
 * Register the corresponding RPC
 * @param mid
 * @param option 0 to 4 (MalleabilityMan, Slurm, IOSched, AdHocMan, Monitor)
 * @return
 */
int RegisterRPC(margo_instance_id& mid, int option){
    /* Filter by option (each thread executes one RPC registration */
    if(option == 0) {
        /*RPC MALLEABILITY INPUT*/
        hg_id_t rpc_malleab_in;
        hg_bool_t flag_mall;
        string malleab_name = "icc_malleabMan";
        margo_provider_registered_name(mid, malleab_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_malleab_in,
                                       &flag_mall);
        if (flag_mall == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            return (EXIT_FAILURE);
        }

        rpc_malleab_in = MARGO_REGISTER_PROVIDER(mid, malleab_name.c_str(),
                                                 malleabilityman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_malleab_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_malleab_in;
        margo_info(mid, "icc_malleability_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 1) {

        /*RPC SLURM INPUT*/
        hg_id_t rpc_slurm_in;
        hg_bool_t flag_slurm;
        string slurm_name = "icc_slurmMan";
        margo_provider_registered_name(mid, slurm_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_slurm_in,
                                       &flag_slurm);
        if (flag_slurm == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            return (EXIT_FAILURE);
        }

        rpc_slurm_in = MARGO_REGISTER_PROVIDER(mid, slurm_name.c_str(),
                                               slurmman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                               rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                               icc_slurm_cb, /* HANDLER CODED BELOW */
                                               ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                               ABT_POOL_NULL);

        (void) rpc_slurm_in;
        margo_info(mid, "icc_slurm_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 2) {

        /*RPC IOSCHED INPUT*/
        hg_id_t rpc_iosched_in;
        hg_bool_t flag_iosched;
        string iosched_name = "icc_iosched";
        margo_provider_registered_name(mid, iosched_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_iosched_in,
                                       &flag_iosched);
        if (flag_iosched == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            return (EXIT_FAILURE);
        }

        rpc_iosched_in = MARGO_REGISTER_PROVIDER(mid, iosched_name.c_str(),
                                                 iosched_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_iosched_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_iosched_in;
        margo_info(mid, "icc_io_scheduler RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 3) {

        /*RPC ADHOC INPUT*/
        hg_id_t rpc_adhoc_in;
        hg_bool_t flag_adhoc;
        string adhoc_name = "icc_adhocMan";
        margo_provider_registered_name(mid, adhoc_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_adhoc_in,
                                       &flag_adhoc);
        if (flag_adhoc == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            return (EXIT_FAILURE);
        }

        rpc_adhoc_in = MARGO_REGISTER_PROVIDER(mid, adhoc_name.c_str(),
                                               adhocman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                               rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                               icc_adhoc_cb, /* HANDLER CODED BELOW */
                                               ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                               ABT_POOL_NULL);

        (void) rpc_adhoc_in;
        margo_info(mid, "icc_adhoc_storage RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else if(option == 4) {

        /*RPC MONITOR INPUT*/
        hg_id_t rpc_monitor_in;
        hg_bool_t flag_mon;
        string mon_name = "icc_monitorMan";
        margo_provider_registered_name(mid, mon_name.c_str(), ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_monitor_in,
                                       &flag_mon);
        if (flag_mon == HG_TRUE) {
            margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
            margo_finalize(mid);
            return (EXIT_FAILURE);
        }

        rpc_monitor_in = MARGO_REGISTER_PROVIDER(mid, mon_name.c_str(),
                                                 monitor_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                                 rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                                 icc_monitor_cb, /* HANDLER CODED BELOW */
                                                 ICC_MARGO_PROVIDER_ID_DEFAULT,/* XX using default Argobot pool */
                                                 ABT_POOL_NULL);

        (void) rpc_monitor_in;
        margo_info(mid, "icc_monitoring_manager RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
    }
    else {
        cerr << "Option " << option << " for RPC registration is not valid." << endl;
        return (EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
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

    /*Malleability manager thread*/
    thread iosched (IOSchedulerRun, mid);

    /*Malleability manager thread*/
    thread adhocMan (AdhocStorageManagerRun, mid);

    /*Malleability manager thread*/
    thread monitMan (MonitoringManagerRun, mid);



    /* Thread join*/
    malleabMan.join();
    slurm.join();
    iosched.join();
    adhocMan.join();
    monitMan.join();


    /*Finish Margo*/
    margo_wait_for_finalize(mid);

    return EXIT_SUCCESS;
}


/**
 * Handler for connections of type Malleability Manager
 * @param h
 */
static void icc_malleab_cb(hg_handle_t h){
    hg_return_t hret;
    malleabilityman_in_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
    rpc_out_t out;/* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/

    out.rc = ICC_SUCCESS;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    if (!mid)
        out.rc = ICC_FAILURE;
    else {
        hret = margo_get_input(h, &in);
        if (hret != HG_SUCCESS) {
            out.rc = ICC_FAILURE;
            margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
        } else {
            margo_info(mid, "Malleability Manager has received: %u\n", in.number);
        }
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not respond to HPC");
    }

    hret = margo_destroy(h);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    }
}
DEFINE_MARGO_RPC_HANDLER(icc_malleab_cb)


/**
 * Handler for connections of type Slurm Manager
 * @param h
 */
static void icc_slurm_cb(hg_handle_t h){
    hg_return_t hret;
    slurmman_in_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
    rpc_out_t out;/* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/

    out.rc = ICC_SUCCESS;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    if (!mid)
        out.rc = ICC_FAILURE;
    else {
        hret = margo_get_input(h, &in);
        if (hret != HG_SUCCESS) {
            out.rc = ICC_FAILURE;
            margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
        } else {
            margo_info(mid, "Slurm Manager has received: %u\n", in.number);
        }
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not respond to HPC");
    }

    hret = margo_destroy(h);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    }
}
DEFINE_MARGO_RPC_HANDLER(icc_slurm_cb)


/**
 * Handler for connections of type I/O Scheduler
 * @param h
 */
static void icc_iosched_cb(hg_handle_t h){
    hg_return_t hret;
    iosched_in_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
    rpc_out_t out;/* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/

    out.rc = ICC_SUCCESS;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    if (!mid)
        out.rc = ICC_FAILURE;
    else {
        hret = margo_get_input(h, &in);
        if (hret != HG_SUCCESS) {
            out.rc = ICC_FAILURE;
            margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
        } else {
            margo_info(mid, "I/O Scheduler has received: %u\n", in.number);
        }
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not respond to HPC");
    }

    hret = margo_destroy(h);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    }

}
DEFINE_MARGO_RPC_HANDLER(icc_iosched_cb)


/**
 * Handler for connections of type AdHoc Storage
 * @param h
 */
static void icc_adhoc_cb(hg_handle_t h){
    hg_return_t hret;
    adhocman_in_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
    rpc_out_t out;/* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/

    out.rc = ICC_SUCCESS;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    if (!mid)
        out.rc = ICC_FAILURE;
    else {
        hret = margo_get_input(h, &in);
        if (hret != HG_SUCCESS) {
            out.rc = ICC_FAILURE;
            margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
        } else {
            margo_info(mid, "Adhoc Storage Manager has received: %u\n", in.number);
        }
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not respond to HPC");
    }

    hret = margo_destroy(h);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    }
}
DEFINE_MARGO_RPC_HANDLER(icc_adhoc_cb)


/**
 * Handler for connections of type Monitoring Manager
 * @param h
 */
static void icc_monitor_cb(hg_handle_t h){
    hg_return_t hret;
    monitor_in_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
    rpc_out_t out;/* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/

    out.rc = ICC_SUCCESS;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    if (!mid)
        out.rc = ICC_FAILURE;
    else {
        hret = margo_get_input(h, &in);
        if (hret != HG_SUCCESS) {
            out.rc = ICC_FAILURE;
            margo_error(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
        } else {
            margo_info(mid, "Monitoring Manager has received: %u\n", in.number);
        }
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not respond to HPC");
    }

    hret = margo_destroy(h);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    }
}
DEFINE_MARGO_RPC_HANDLER(icc_monitor_cb)

