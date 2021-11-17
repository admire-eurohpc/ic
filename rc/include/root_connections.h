//
// Created by Alberto Cascajo on 15/11/21.
// This class contains the structs and functions related to the connections with the root controller
//

#ifndef RC_ROOT_CONTROLLER_H
#define RC_ROOT_CONTROLLER_H

/* Include ICC files */
#include <margo.h>


/*Function interfaces*/
int load_environment_controller(margo_instance_id&);


int register_rpc(margo_instance_id&, int);


/*RPCs for Root controller*/

static void icc_malleab_cb_in(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_malleab_cb_in)


static void icc_malleab_cb_out(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_malleab_cb_out)

static void icc_slurm_cb_in(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_slurm_cb_in)


static void icc_slurm_cb_out(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_slurm_cb_out)

static void icc_iosched_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_iosched_cb)

static void icc_adhoc_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_adhoc_cb)

static void icc_monitor_cb_out(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_monitor_cb_out)


#endif //RC_ROOT_CONTROLLER_H
