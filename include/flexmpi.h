#ifndef __ADMIRE_FLEXMPI_H
#define __ADMIRE_FLEXMPI_H

#include <margo.h>


typedef int (*flexmpi_reconfigure_t)(const char *); /* signature of the reconfigure func */


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
 * Reconfigure a FlexMPI application by adding or removing MAXPROCS,
 * depending on whether this number is positive, by using FLEXMPIFUNC
 * or FLEXMPISOCK is this function is NULL. HOSTLIST is unused.
 *
 * Return the result of FLEXMPIFUNC or -1. XX no difference between a
 * socket use and an error.
 */
int flexmpi_reconfigure(margo_instance_id mid, uint32_t maxprocs, const char *hostlist, flexmpi_reconfigure_t flexmpifunc, int flexmpisock);

#endif
