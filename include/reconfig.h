#ifndef __ADMIRE_RECONFIGURE_H
#define __ADMIRE_RECONFIGURE_H

#include <margo.h>


typedef int (*flexmpi_reconfigure_t)(const char *); /* signature of the reconfigure func */

struct reconfig_data {
  icc_reconfigure_func_t func;
  void                   *data;
  enum icc_client_type   type;
  /* XX TMP: flexmpi specific */
  int                   flexmpisock;
  flexmpi_reconfigure_t flexmpifunc;
};


/**
 * Initialize a socket to communicate with the FlexMPI application
 * identified by address NODE and port SERVICE.
 *
 * Return a socket descriptor or -1 in case of error.
 */
int flexmpi_socket(margo_instance_id mid, const char *node, const char *service);


/**
 * Get the FlexMPI function responsible for reconfiguration for use in
 * a ICC callback.
 *
 * Return a pointer to the function or NULL in case of error. The
 * dlopen handle is also returned in HANDLE.
 */
flexmpi_reconfigure_t flexmpi_func(margo_instance_id mid, void **handle);


/**
 * Reconfigure callback.
 *
 * RPC status code:
 * RPC_SUCCESS or RPC_FAILURE in case of error.
 */
void reconfigure_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(reconfigure_cb);

#endif
