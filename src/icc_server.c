#include <errno.h>
#include <inttypes.h>         /* PRIdxx */
#include <stdio.h>
#include <margo.h>

#include "icc_rpc.h"
#include "icc_common.h"
#include "icdb.h"


static void test_cb(hg_handle_t h);
static void jobmon_submit_cb(hg_handle_t h);
static void jobmon_exit_cb(hg_handle_t h);
static void adhoc_nodes_cb(hg_handle_t h);

/* XX bad global variable? */
static struct icdb_context *icdb = NULL;


int
main(int argc __attribute__((unused)), char** argv __attribute__((unused)))
{
  margo_instance_id mid;

  mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
  if (!mid) {
    margo_error(mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
    return ICC_FAILURE;
  }
  margo_set_log_level(mid, MARGO_LOG_INFO);

  char addr_str[ICC_ADDR_MAX_SIZE];
  hg_size_t addr_str_size = ICC_ADDR_MAX_SIZE;
  if (icc_hg_addr(mid, addr_str, &addr_str_size)) {
    margo_error(mid, "Could not get Mercury address");
    margo_finalize(mid);
    return ICC_FAILURE;
  }

  margo_info(mid, "Margo Server running at address %s", addr_str);

  /* Write Mercury address to file */
  char *path = icc_addr_file();
  if (!path) {
    margo_error(mid, "Could not get ICC address file");
    margo_finalize(mid);
    return ICC_FAILURE;
  }

  FILE *f = fopen(path, "w");
  if (f == NULL) {
    margo_error(mid, "Could not open ICC address file \"%s\": %s", path, strerror(errno));
    free(path);
    margo_finalize(mid);
    return ICC_FAILURE;
  }
  free(path);

  int nbytes = fprintf(f, "%s", addr_str);
  if (nbytes < 0 || (unsigned)nbytes != addr_str_size - 1) {
    margo_error(mid, "Error writing to address file", strerror(errno));
    fclose(f);
    margo_finalize(mid);
    return ICC_FAILURE;
  }
  fclose(f);

  /* Register RPCs */
  hg_id_t rpc_ids[ICC_RPC_COUNT] = { 0 };             /* RPC id table */
  icc_callback_t callbacks[ICC_RPC_COUNT] = { NULL }; /* callback table */

  hg_bool_t flag;
  margo_provider_registered_name(mid, "icc_test", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_ids[ICC_RPC_COUNT], &flag);
  if(flag == HG_TRUE) {
    margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
    margo_finalize(mid);
    return ICC_FAILURE;
  }

  ICC_RPC_PREPARE(rpc_ids, callbacks, ICC_RPC_TEST, test_cb);
  ICC_RPC_PREPARE(rpc_ids, callbacks, ICC_RPC_JOBMON_SUBMIT, jobmon_submit_cb);
  ICC_RPC_PREPARE(rpc_ids, callbacks, ICC_RPC_JOBMON_EXIT, jobmon_exit_cb);
  ICC_RPC_PREPARE(rpc_ids, callbacks, ICC_RPC_ADHOC_NODES, adhoc_nodes_cb);

  register_rpcs(mid, callbacks, rpc_ids);

  /* initialize connection to DB */
  int icdb_rc;
  icdb_rc = icdb_init(&icdb);
  if (!icdb) {
    margo_error(mid, "Could not initialize IC database");
    margo_finalize(mid);
    return ICC_FAILURE;
  }
  else if (icdb_rc != ICDB_SUCCESS) {
    margo_error(mid, "Could not initialize IC database: %s", icdb_errstr(icdb));
    margo_finalize(mid);
    return ICC_FAILURE;
  }

  margo_wait_for_finalize(mid);

  /* close connection to DB */
  icdb_fini(&icdb);

  return ICC_SUCCESS;
}


static void
test_cb(hg_handle_t h)
{
  hg_return_t hret;
  test_in_t in;
  rpc_out_t out;

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
      margo_info(mid, "Got \"test\" RPC with argument %u\n", in.number);
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


static void
jobmon_submit_cb(hg_handle_t h)
{
  hg_return_t hret;

  jobmon_submit_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  margo_instance_id mid = margo_hg_handle_get_instance(h);
  if (mid) {
    hret = margo_get_input(h, &in);
    if (hret != HG_SUCCESS) {
      out.rc = ICC_FAILURE;
      margo_error(mid, "Could not get RPC input");
    }

    margo_info(mid, "Slurm Job %"PRId32".%"PRId32" started on %"PRId32" node%s",
               in.slurm_jobid, in.slurm_jobstepid, in.slurm_nnodes, in.slurm_nnodes > 1 ? "s" : "");

    int icdb_rc = icdb_command(icdb, "SET nnodes:%"PRId32".%"PRId32" %"PRId32,
                               in.slurm_jobid, in.slurm_jobstepid, in.slurm_nnodes);
    if (icdb_rc != ICDB_SUCCESS) {
      margo_error(mid, "Could not write to IC database: %s", icdb_errstr(icdb));
    }

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
      margo_error(mid, "Could not respond to HPC");
    }
  }

  hret = margo_destroy(h);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
  }
}


static void
jobmon_exit_cb(hg_handle_t h)
{
  hg_return_t hret;

  jobmon_submit_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  margo_instance_id mid = margo_hg_handle_get_instance(h);
  if (mid) {
    hret = margo_get_input(h, &in);
    if (hret != HG_SUCCESS) {
      out.rc = ICC_FAILURE;
      margo_error(mid, "Could not get RPC input");
    }

    margo_info(mid, "Slurm Job %"PRId32".%"PRId32" exited", in.slurm_jobid, in.slurm_jobstepid);

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
      margo_error(mid, "Could not respond to HPC");
    }
  }

  hret = margo_destroy(h);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
  }
}


static void
adhoc_nodes_cb(hg_handle_t h)
{
  hg_return_t hret;

  adhoc_nodes_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  margo_instance_id mid = margo_hg_handle_get_instance(h);
  if (mid) {
    hret = margo_get_input(h, &in);
    if (hret != HG_SUCCESS) {
      out.rc = ICC_FAILURE;
      margo_error(mid, "Could not get RPC input");
    }

    margo_info(mid, "IC got adhoc_nodes request from job %"PRId32": %"PRId32" nodes (%"PRId32" nodes assigned by Slurm)",
               in.slurm_jobid, in.adhoc_nnodes, in.slurm_nnodes);

    hret = margo_respond(h, &out);
    if (hret != HG_SUCCESS) {
      margo_error(mid, "Could not respond to HPC");
    }
  }

  hret = margo_destroy(h);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
  }
}
