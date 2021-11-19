//
// Created by Alberto Cascajo on 15/11/21.
//


/* Include ICC files */
#include <margo.h>
#include <errno.h>
#include "icc_rpc.h"
#include "root_connections.h"


/* HANDLERS */

/**
 * Handler for IN connections of type Malleability Manager
 * @param h
 */
static void
icc_malleab_cb_in(hg_handle_t h)
{
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
DEFINE_MARGO_RPC_HANDLER(icc_malleab_cb_in)


/**
 * Handler for OUT connections of type Malleability Manager
 * @param h
 */
static void
icc_malleab_cb_out(hg_handle_t h)
{
  hg_return_t hret;
  malleabilityman_out_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
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
DEFINE_MARGO_RPC_HANDLER(icc_malleab_cb_out)


/**
 * Handler for IN connections of type Slurm Manager (Root controller)
 * @param h
 */
static void
icc_slurm_cb_in(hg_handle_t h)
{
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
DEFINE_MARGO_RPC_HANDLER(icc_slurm_cb_in)

/**
 * Handler for OUT connections of type Slurm Manager (Slurm module)
 * @param h
 */
static void
icc_slurm_cb_out(hg_handle_t h)
{
  hg_return_t hret;
  slurmman_out_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
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
DEFINE_MARGO_RPC_HANDLER(icc_slurm_cb_out)


/**
 * Handler for connections of type I/O Scheduler
 * @param h
 */
static void
icc_iosched_cb(hg_handle_t h)
{
  hg_return_t hret;
  iosched_out_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
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
static void
icc_adhoc_cb(hg_handle_t h)
{
  hg_return_t hret;
  adhocman_out_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
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
 * Handler for OUT connections of type Monitoring Manager
 * @param h
 */
static void
icc_monitor_cb_out(hg_handle_t h) {
  hg_return_t hret;
  monitor_out_t in; /* CHANGE TYPE WHEN IT CHANGE IN RPC DEFINITION*/
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
DEFINE_MARGO_RPC_HANDLER(icc_monitor_cb_out)





/* FUNCTIONS RPC REGISTERING */

/**
 *  Initialize Margo variables and run the environment
  * @param id of margo instance
  * @return 0 success, -1 failure
  */
int
load_environment_controller(margo_instance_id mid) {
  mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
  if (!mid) {
    margo_error(mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
    return (EXIT_FAILURE);
  }
  margo_set_log_level(mid, MARGO_LOG_INFO);

  if (margo_is_listening(mid) == HG_FALSE) {
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
  if (nbytes < 0 || (unsigned) nbytes != addr_str_size - 1) {
    margo_error(mid, "Error writing to address file", strerror(errno));
    fclose(f);
    margo_finalize(mid);
    return (EXIT_FAILURE);
  }
  fclose(f);


  return (EXIT_SUCCESS);
}

int
load_environment_controller_opt(margo_instance_id *mid, int opt) {
  printf("Setting up the margo server opt %d\n", opt);
  *mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
  if (*mid == NULL) {
    margo_error(*mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
    return (EXIT_FAILURE);
  }
  margo_set_log_level(*mid, MARGO_LOG_INFO);

  if (margo_is_listening(*mid) == HG_FALSE) {
    margo_error(*mid, "Margo instance is not a server");
    margo_finalize(*mid);
    return (EXIT_FAILURE);
  }

  hg_return_t hret;
  hg_addr_t addr;
  hg_size_t addr_str_size = ICC_ADDR_MAX_SIZE;
  char addr_str[ICC_ADDR_MAX_SIZE];

  hret = margo_addr_self(*mid, &addr);
  if (hret != HG_SUCCESS) {
    margo_error(*mid, "Could not get Margo self address");
    margo_finalize(*mid);
    return (EXIT_FAILURE);
  }

  hret = margo_addr_to_string(*mid, addr_str, &addr_str_size, addr);
  if (hret != HG_SUCCESS) {
    margo_error(*mid, "Could not convert Margo self address to string");
    margo_addr_free(*mid, addr);
    margo_finalize(*mid);
    return (EXIT_FAILURE);
  }

  hret = margo_addr_free(*mid, addr);
  if (hret != HG_SUCCESS)
    margo_error(*mid, "Could not free Margo address");

  margo_info(*mid, "Margo Server running at address %s", addr_str);

  char *path = icc_addr_file_opt(opt);
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    margo_error(*mid, "Could not open address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    margo_finalize(*mid);
    return (EXIT_FAILURE);
  }
  free(path);

  int nbytes = fprintf(f, "%s", addr_str);
  if (nbytes < 0 || (unsigned) nbytes != addr_str_size - 1) {
    margo_error(*mid, "Error writing to address file", strerror(errno));
    fclose(f);
    margo_finalize(*mid);
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
int
register_rpc(margo_instance_id mid, int option)
{
  /*
   * Filter by RPC ID (defined in root_connections.h)
   */

  if (option == ADM_GETSYSTEMSTATUS) { /* ADM_getSystemStatus RPC */
    hg_id_t rpc_malleab_in;
    hg_bool_t flag_mall;
    margo_provider_registered_name(mid, "icc_malleabMan_in", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_malleab_in,
                                   &flag_mall);
    if (flag_mall == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc_malleab_in = MARGO_REGISTER_PROVIDER(mid, "icc_malleabMan_in",
                                             malleabilityman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                             rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                             icc_malleab_cb_in, /* HANDLER CODED */
                                             ICC_MARGO_PROVIDER_ID_DEFAULT,
                                             ABT_POOL_NULL);

    (void) rpc_malleab_in;
    margo_info(mid, "GetSystemStatus RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else if (option == ADM_JOBSCHEDULESOLUTION) { /* ADM_jobScheduleSolution RPC */
    hg_id_t rpc_slurm_in;
    hg_bool_t flag_slurm;
    margo_provider_registered_name(mid, "icc_slurmMan_in", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_slurm_in,
                                   &flag_slurm);
    if (flag_slurm == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc_slurm_in = MARGO_REGISTER_PROVIDER(mid, "icc_slurmMan_in",
                                           slurmman_in_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                           rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                           icc_slurm_cb_in, /* HANDLER CODED */
                                           ICC_MARGO_PROVIDER_ID_DEFAULT,
                                           ABT_POOL_NULL);

    (void) rpc_slurm_in;
    margo_info(mid, "JobScheduleSolution RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else if (option == ADM_IOSCHEDULESOLUTION) { /* ADM_IOScgeduleSolution RPC */
    hg_id_t rpc_iosched_out;
    hg_bool_t flag_iosched;
    margo_provider_registered_name(mid, "icc_iosched_out", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_iosched_out,
                                   &flag_iosched);
    if (flag_iosched == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc_iosched_out = MARGO_REGISTER_PROVIDER(mid, "icc_iosched_out",
                                              iosched_out_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                              rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                              icc_iosched_cb, /* HANDLER CODED  */
                                              ICC_MARGO_PROVIDER_ID_DEFAULT,
                                              ABT_POOL_NULL);

    (void) rpc_iosched_out;
    margo_info(mid, "IOScheduleSolution RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else if (option == ADM_IOSTATUS) { /* ADM_IOStatus RPC */
    hg_id_t rpc_adhoc_out;
    hg_bool_t flag_adhoc;
    margo_provider_registered_name(mid, "icc_adhocMan_out", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_adhoc_out,
                                   &flag_adhoc);
    if (flag_adhoc == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc_adhoc_out = MARGO_REGISTER_PROVIDER(mid, "icc_adhocMan_out",
                                            adhocman_out_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                            rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                            icc_adhoc_cb, /* HANDLER CODED  */
                                            ICC_MARGO_PROVIDER_ID_DEFAULT,
                                            ABT_POOL_NULL);

    (void) rpc_adhoc_out;
    margo_info(mid, "IOStatus RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else if (option == ADM_SLURMSTATE) { /* ADM_SlurmState RPC */
    hg_id_t rpc_slurm_out;
    hg_bool_t flag_slurm;
    margo_provider_registered_name(mid, "icc_slurmMan_out", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_slurm_out,
                                   &flag_slurm);
    if (flag_slurm == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc_slurm_out = MARGO_REGISTER_PROVIDER(mid, "icc_slurmMan_out",
                                            slurmman_out_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                            rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                            icc_slurm_cb_out, /* HANDLER CODED  */
                                            ICC_MARGO_PROVIDER_ID_DEFAULT,
                                            ABT_POOL_NULL);

    (void) rpc_slurm_out;
    margo_info(mid, "SlurmState RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else if (option == ADM_SUGGESTSCHEDULESOLUTION) { /* ADM_SuggestScheduleSolution RPC */
    hg_id_t rpc;
    hg_bool_t flag;
    margo_provider_registered_name(mid, "icc_malleabMan_out", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc,
                                   &flag);
    if (flag == HG_TRUE) {
      margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
      margo_finalize(mid);
      return (EXIT_FAILURE);
    }

    rpc = MARGO_REGISTER_PROVIDER(mid, "icc_malleabMan_out",
                                  malleabilityman_out_t, /* CURRENTLY ALL RPCS HAVE THE SAME TYPE. IT WILL CHANGE (ICC_RPC.H) */
                                            rpc_out_t, /* GENERAL OUTPUT: RETCODE. IN SOME CASES IT WILL CHANGE */
                                            icc_malleab_cb_out, /* HANDLER CODED  */
                                            ICC_MARGO_PROVIDER_ID_DEFAULT,
                                            ABT_POOL_NULL);

    (void) rpc;
    margo_info(mid, "SuggestScheduleSolution RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);
  }
  else {
    printf("Option %d for RPC registration is not valid.\n", option);
    return (EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}



