#include <errno.h>
#include <margo.h>

#include "ic.h"
#include "ic_rpc.h"



/* TODO
 - factorize with goto?
 - hg_error_to_string everywhere
 - IC_RETCODE?
 - translate Margo log levels? (remove margo.h dependency on client)
*/


struct ic_context {
  margo_instance_id mid;
  hg_addr_t addr;
};


/* Returns NULL on error */
struct ic_context *ic_init(margo_log_level loglevel) {
  hg_return_t hret;

  struct ic_context *icc = calloc(1, sizeof(struct ic_context));
  if (!icc) {
    margo_error(icc->mid, "Could not allocate memory: %s", strerror(errno));
    return NULL;
  }

  if (!(icc->mid = margo_init(IC_HG_PROVIDER, MARGO_CLIENT_MODE, 0, 0))) {
    margo_error(icc->mid, "Error initializing Margo instance with provider %s", IC_HG_PROVIDER);
    return NULL;
  }

  margo_set_log_level(icc->mid, loglevel);

  FILE *f = fopen(IC_ADDR_FILE, "r");
  if (!f) {
    margo_error(icc->mid, "Error opening Margo address file \""IC_ADDR_FILE"\": %s", strerror(errno));
    margo_finalize(icc->mid);
    return NULL;
  }

  char addr_str[IC_ADDR_MAX_SIZE];
  if (!fgets(addr_str, IC_ADDR_MAX_SIZE, f)) {
    margo_error(icc->mid, "Error reading from Margo address file: %s", strerror(errno));
    fclose(f);
    margo_finalize(icc->mid);
    return NULL;
  }
  fclose(f);

  if ((hret = margo_addr_lookup(icc->mid, addr_str, &icc->addr)) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get Margo address: %s", HG_Error_to_string(hret));
    margo_finalize(icc->mid);
    return NULL;
  }

  return icc;
}


int ic_fini(struct ic_context *icc) {
  int rc = EXIT_SUCCESS;

  if (!icc)
    return rc;

  if (margo_addr_free(icc->mid, icc->addr) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not free Margo address");
    rc = EXIT_FAILURE;
  }
  margo_finalize(icc->mid);
  free(icc);
  return rc;
}


int ic_make_rpc(struct ic_context *icc) {
  hg_return_t hret;
  hg_handle_t handle;
  hello_out_t resp;
  hg_id_t hello_rpc_id = MARGO_REGISTER(icc->mid, "hello", void, hello_out_t, NULL);

  if ((hret = margo_create(icc->mid, icc->addr, hello_rpc_id, &handle)) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not create Margo RPC: %s", HG_Error_to_string(hret));
    margo_finalize(icc->mid);
    return EXIT_FAILURE;
  }

  if ((hret = margo_forward(handle, NULL)) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not forward Margo RPC: %s", HG_Error_to_string(hret));
    margo_destroy(handle);	/* XX check error */
    margo_finalize(icc->mid);
    return EXIT_FAILURE;
  }

  if ((hret = margo_get_output(handle, &resp)) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not get RPC output: %s", HG_Error_to_string(hret));
  }
  else {
    margo_info(icc->mid, "Output is \"%s\" (retcode=%d)", resp.msg, resp.rc);

    if ((hret = margo_free_output(handle, &resp)) != HG_SUCCESS) {
      margo_error(icc->mid, "Could not free RPC output: %s", HG_Error_to_string(hret));
    }
  }

  if ((hret = margo_destroy(handle)) != HG_SUCCESS) {
    margo_error(icc->mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret));
    margo_finalize(icc->mid);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
