#ifndef __ADMIRE_FLEXMPI_H
#define __ADMIRE_FLEXMPI_H

#include <margo.h>


#define FLEXMPI_COMMAND_MAX_LEN 256

typedef int (*flexmpi_reconfigure_t)(const char *); /* signature of the reconfigure func */

struct flexmpi_cbdata {
  int sock;
  flexmpi_reconfigure_t func;
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
 * Return a pointer to the function or NULL in case of error.
 */
flexmpi_reconfigure_t flexmpi_func(margo_instance_id mid);


/**
 * Forward the  malleability command to the FlexMPI socket.
 *
 * RPC answers:
 * RPC_OK if everything went fine
 * RPC_E2BIG if the command is too long to fit in the buffer.
 *
 */
void flexmpi_malleability_cb(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(flexmpi_malleability_cb);

#endif
