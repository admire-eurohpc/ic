#include <margo.h>

#include "flexmpi.h"
#include "icc_rpc.h"


static char command[FLEXMPI_COMMAND_MAX_LEN];

void
flexmpi_malleability_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  flexmpi_malleability_in_t in;
  rpc_out_t out;

  out.rc = FLEXMPI_OK;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "Error getting FLEXMPI_MALLEABILITY RPC input: %s", HG_Error_to_string(hret));
    goto respond;
  }

  if (strnlen(in.command, FLEXMPI_COMMAND_MAX_LEN) == FLEXMPI_COMMAND_MAX_LEN) {
    out.rc = FLEXMPI_COMM2BIG;
    goto respond;
  }

  strcpy(command, in.command);
  margo_info(mid, "MALLEABILITY command is \"%s\"", command);

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Could not respond to HPC");
  }
}


void
flexmpi_malleability_th(void *arg)
{
  ;
}
