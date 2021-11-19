#include <errno.h>
#include <margo.h>
#include <stdlib.h>		/* malloc */
#include <string.h>

#include "../include/icc_rpc.h"


/* TODO
 * factorize with goto?
 * hg_error_to_string everywhere
 * Margo logging inside lib?
 * icc_status RPC (~dummy?)
 * Return struct or rc only?
 * XX malloc intempestif,
 * Cleanup error/info messages
 * margo_free_input in callback!
 * icc_init error code errno vs ICC? cleanup all
 * do we need an include directory?
*/


struct icc_context {
  margo_instance_id mid;
  hg_addr_t         addr;
  uint16_t          provider_id;
};


static hg_id_t rpc_hg_ids[ICC_RPC_COUNT];


int
icc_init(enum icc_log_level log_level, struct icc_context **icc_context)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return -errno;

  icc->mid = margo_init(ICC_HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0);
  if (!icc->mid) {
    rc = ICC_FAILURE;
    goto error;
  }

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  char *path = icc_addr_file();
  FILE *f = fopen(path, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = -errno;
    goto error;
  }
  free(path);

  char addr_str[ICC_ADDR_MAX_SIZE];
  if (!fgets(addr_str, ICC_ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from Margo address file: %s", strerror(errno));
    fclose(f);
    rc = -errno;
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto error;
  }

  icc->provider_id = ICC_MARGO_PROVIDER_ID_DEFAULT;

  /* RPCs */
  rpc_hg_ids[ICC_RPC_TEST] = MARGO_REGISTER(icc->mid, "icc_test", test_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_IN] = MARGO_REGISTER(icc->mid, "icc_malleabMan_in", malleabilityman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_OUT] = MARGO_REGISTER(icc->mid, "icc_malleabMan_out", malleabilityman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_IN] = MARGO_REGISTER(icc->mid, "icc_slurmMan_in", slurmman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_OUT] = MARGO_REGISTER(icc->mid, "icc_slurmMan_out", slurmman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_IOSCHED_OUT] = MARGO_REGISTER(icc->mid, "icc_iosched_out", iosched_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_OUT] = MARGO_REGISTER(icc->mid, "icc_adhocMan_out", adhocman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MONITOR_OUT] = MARGO_REGISTER(icc->mid, "icc_monitorMan_out", monitor_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_SUBMIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_submit", jobmon_submit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_EXIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_exit", jobmon_exit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, "icc_adhoc_nodes", adhoc_nodes_in_t, rpc_out_t, NULL);



  /* register other RPCs here */

  *icc_context = icc;
  return ICC_SUCCESS;

 error:
  if (icc) {
    if (icc->mid)
      margo_finalize(icc->mid);
    free(icc);
  }
  return rc;
}


int
icc_init_opt(enum icc_log_level log_level, struct icc_context **icc_context, int server_id)
{
  hg_return_t hret;
  int rc = ICC_SUCCESS;

  *icc_context = NULL;

  struct icc_context *icc = calloc(1, sizeof(struct icc_context));
  if (!icc)
    return -errno;

  icc->mid = margo_init(ICC_HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0);
  if (!icc->mid) {
    rc = ICC_FAILURE;
    goto error;
  }

  margo_set_log_level(icc->mid, icc_to_margo_log_level(log_level));

  char *path = icc_addr_file_opt(server_id);
  FILE *f = fopen(path, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \"%s\": %s", path ? path : "(NULL)", strerror(errno));
    free(path);
    rc = -errno;
    goto error;
  }
  free(path);

  char addr_str[ICC_ADDR_MAX_SIZE];
  if (!fgets(addr_str, ICC_ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from Margo address file: %s", strerror(errno));
    fclose(f);
    rc = -errno;
    goto error;
  }
  fclose(f);

  hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address: %s", HG_Error_to_string(hret));
    rc = ICC_FAILURE;
    goto error;
  }

  icc->provider_id = ICC_MARGO_PROVIDER_ID_DEFAULT;

  /* RPCs */
  rpc_hg_ids[ICC_RPC_TEST] = MARGO_REGISTER(icc->mid, "icc_test", test_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_IN] = MARGO_REGISTER(icc->mid, "icc_malleabMan_in", malleabilityman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MALLEABILITY_OUT] = MARGO_REGISTER(icc->mid, "icc_malleabMan_out", malleabilityman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_IN] = MARGO_REGISTER(icc->mid, "icc_slurmMan_in", slurmman_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_SLURM_OUT] = MARGO_REGISTER(icc->mid, "icc_slurmMan_out", slurmman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_IOSCHED_OUT] = MARGO_REGISTER(icc->mid, "icc_iosched_out", iosched_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_OUT] = MARGO_REGISTER(icc->mid, "icc_adhocMan_out", adhocman_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_MONITOR_OUT] = MARGO_REGISTER(icc->mid, "icc_monitorMan_out", monitor_out_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_SUBMIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_submit", jobmon_submit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_JOBMON_EXIT] = MARGO_REGISTER(icc->mid, "icc_jobmon_exit", jobmon_exit_in_t, rpc_out_t, NULL);
  rpc_hg_ids[ICC_RPC_ADHOC_NODES] = MARGO_REGISTER(icc->mid, "icc_adhoc_nodes", adhoc_nodes_in_t, rpc_out_t, NULL);



  /* register other RPCs here */

  *icc_context = icc;
  return ICC_SUCCESS;

  error:
  if (icc) {
    if (icc->mid)
      margo_finalize(icc->mid);
    free(icc);
  }
  return rc;
}


int
icc_fini(struct icc_context *icc)
{
  int rc = ICC_SUCCESS;

  if (!icc)
    return rc;

  if (margo_addr_free(icc->mid, icc->addr) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not free Margo address");
    rc = ICC_FAILURE;
  }

  margo_finalize(icc->mid);
  free(icc);
  return rc;
}


int
icc_rpc_send(struct icc_context *icc, enum icc_rpc_code rpc_code, void *data, int *retcode) {
  hg_return_t hret;
  hg_handle_t handle;

  if (!icc)
    return ICC_FAILURE;

  if (!data) {
    margo_error(icc->mid, "Null RPC data argument");
    return ICC_FAILURE;
  }

  switch (rpc_code) {
  case ICC_RPC_TEST:
  case ICC_RPC_ADHOC_NODES:
  case ICC_RPC_JOBMON_SUBMIT:
  case ICC_RPC_JOBMON_EXIT:
  case ICC_RPC_MALLEABILITY_IN:
  case ICC_RPC_MALLEABILITY_OUT:
  case ICC_RPC_SLURM_IN:
  case ICC_RPC_SLURM_OUT:
  case ICC_RPC_IOSCHED_OUT:
  case ICC_RPC_ADHOC_OUT:
  case ICC_RPC_MONITOR_OUT:
    break;
  default:
    margo_error(icc->mid, "Unknown ICC RPC id %d", rpc_code);
    return ICC_FAILURE;
  }

  hret = margo_create(icc->mid, icc->addr, rpc_hg_ids[rpc_code], &handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  /* XX cast public struct to HG struct, hackish and dangerous */
  hret = margo_provider_forward_timed(icc->provider_id, handle, data, ICC_RPC_TIMEOUT_MS);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);      /* XX check error */
    return ICC_FAILURE;
  }

  rpc_out_t resp;
  hret = margo_get_output(handle, &resp);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    *retcode = resp.rc;

    hret = margo_free_output(handle, &resp);
    if (hret != HG_SUCCESS) {
      margo_error(icc->mid, "Could not free RPC output: %s", HG_Error_to_string(hret));
    }
  }

  hret = margo_destroy(handle);
  if (hret != HG_SUCCESS) {
    margo_error(icc->mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    return ICC_FAILURE;
  }

  return ICC_SUCCESS;
}
