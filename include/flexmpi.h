#ifndef __ADMIRE_FLEXMPI_H
#define __ADMIRE_FLEXMPI_H

#include <margo.h>

#define FLEXMPI_OK  0
#define FLEXMPI_ERR -1

#define FLEXMPI_COMM2BIG 1      /* malleability command is too long */


#define FLEXMPI_COMMAND_MAX_LEN 256

/**
 * Store the FlexMPI malleability command from
 * ICC_RPC_FLEXMPI_MALLEABILITY RPC in an internal buffer.
 *
 * RPC answers:
 * FLEXMPI_OK if everything went fine
 * FLEXMPI_COMM2BIG if the command is too long to fit in the buffer.
 *
 */
void flexmpi_malleability_cb(hg_handle_t h, margo_instance_id mid);


/**
 * Handle background communication with the FlexMPI socket.
 */
void flexmpi_malleability_th(void *arg);

#endif
