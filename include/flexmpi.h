#ifndef __ADMIRE_FLEXMPI_H
#define __ADMIRE_FLEXMPI_H

#include <margo.h>


/**
 * Initialize a socket to communicate with the FlexMPI application
 * identified by address NODE and port SERVICE.
 *
 * Return a socket descriptor or -1 in case of error.
 */
int flexmpi_socket(margo_instance_id mid, const char *node, const char *service);


/**
 * Forward the  malleability command to the FlexMPI socket.
 *
 * RPC answers:
 * RPC_OK if everything went fine
 * RPC_E2BIG if the command is too long to fit in the buffer.
 *
 */
void flexmpi_malleability_cb(hg_handle_t h);
DEFINE_MARGO_RPC_HANDLER(flexmpi_malleability_cb);

#endif
