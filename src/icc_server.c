#include <errno.h>
#include <inttypes.h>         /* PRIdxx */
#include <stdio.h>
#include <margo.h>

#include "icc_rpc.h"


static void icc_test_cb(hg_handle_t h);
static void icc_adhoc_nodes_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(icc_test_cb) /* place the cb in an Argobots ULT */
DECLARE_MARGO_RPC_HANDLER(icc_adhoc_nodes_cb)


int
main(int argc __attribute__((unused)), char** argv __attribute__((unused)))
{

  margo_instance_id mid;

  mid = margo_init(ICC_HG_PROVIDER, MARGO_SERVER_MODE, 0, -1);
  if (!mid) {
    margo_error(mid, "Error initializing Margo instance with provider %s", ICC_HG_PROVIDER);
    return EXIT_FAILURE;
  }
  margo_set_log_level(mid, MARGO_LOG_INFO);

  if(margo_is_listening(mid) == HG_FALSE) {
    margo_error(mid, "Margo instance is not a server");
    margo_finalize(mid);
    return EXIT_FAILURE;
  }

  hg_return_t hret;
  hg_addr_t addr;
  hg_size_t addr_str_size = ICC_ADDR_MAX_SIZE;
  char addr_str[ICC_ADDR_MAX_SIZE];

  hret = margo_addr_self(mid, &addr);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not get Margo self address");
    margo_finalize(mid);
    return EXIT_FAILURE;
  }

  hret = margo_addr_to_string(mid, addr_str, &addr_str_size, addr);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not convert Margo self address to string");
    margo_addr_free(mid, addr);
    margo_finalize(mid);
    return EXIT_FAILURE;
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
    return EXIT_FAILURE;
  }
  free(path);

  int nbytes = fprintf(f, "%s", addr_str);
  if (nbytes < 0 || (unsigned)nbytes != addr_str_size - 1) {
    margo_error(mid, "Error writing to address file", strerror(errno));
    fclose(f);
    margo_finalize(mid);
    return EXIT_FAILURE;
  }
  fclose(f);

  hg_id_t rpc_test_id;
  hg_bool_t flag;
  margo_provider_registered_name(mid, "icc_test", ICC_MARGO_PROVIDER_ID_DEFAULT, &rpc_test_id, &flag);
  if(flag == HG_TRUE) {
    margo_error(mid, "Provider %d already exists", ICC_MARGO_PROVIDER_ID_DEFAULT);
    margo_finalize(mid);
    return EXIT_FAILURE;
  }

  rpc_test_id = MARGO_REGISTER_PROVIDER(mid, "icc_test",
					 test_in_t,
					 rpc_out_t,
					 icc_test_cb,
					 ICC_MARGO_PROVIDER_ID_DEFAULT,
					 /* XX using default Argobot pool */
					 ABT_POOL_NULL);

  (void)rpc_test_id;
  margo_info(mid, "icc_test RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);

  /* Ad-hoc storage RPCs */
  hg_id_t rpc_adhoc_nodes_id;
  rpc_adhoc_nodes_id = MARGO_REGISTER_PROVIDER(mid, "icc_adhoc_nodes",
					       adhoc_nodes_in_t,
					       rpc_out_t,
					       icc_adhoc_nodes_cb,
					       ICC_MARGO_PROVIDER_ID_DEFAULT,
					       ABT_POOL_NULL);
  (void) rpc_adhoc_nodes_id;
  margo_info(mid, "icc_adhoc_nodes RPC registered to provider %d", ICC_MARGO_PROVIDER_ID_DEFAULT);

  /* register other RPCs here */

  margo_wait_for_finalize(mid);

  return EXIT_SUCCESS;
}


static void
icc_test_cb(hg_handle_t h)
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
DEFINE_MARGO_RPC_HANDLER(icc_test_cb)


static void
icc_adhoc_nodes_cb(hg_handle_t h)
{
  hg_return_t hret;

  adhoc_nodes_in_t in;
  adhoc_nodes_out_t out;

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
DEFINE_MARGO_RPC_HANDLER(icc_adhoc_nodes_cb)
