#include <assert.h>
#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>
#include <netdb.h>              /* sockets */
#include <sys/socket.h>         /* sockets */
#include <sys/types.h>          /* sockets */
#include <unistd.h>             /* close */
#include <margo.h>

#include "flexmpi.h"


/* XX TMP: FlexMPI specific */
#define LIBEMPI_SO "libempi.so"                   /* FlexMPI library */
#define FLEXMPI_RECONFIGURE "flexmpi_reconfigure" /* FlexMPI reconfigure func */
#define FLEXMPI_COMMAND_LEN 256

static ABT_mutex_memory mutexmem = ABT_MUTEX_INITIALIZER;


int
flexmpi_socket(margo_instance_id mid, const char *node, const char *service)
{
  /* node = "localhost", service = "7670" */
  struct addrinfo hints, *res, *p;
  int sock, ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;      /* FlexMPI only supports IPV4 */
  hints.ai_socktype = SOCK_DGRAM; /* datagram socket */

  ret = getaddrinfo(node, service, &hints, &res);
  if (ret != 0) {
    margo_error(mid, "%s: getaddrinfo failed: %s", __func__, gai_strerror(ret));
    return -1;
  }

  /* get first valid socket */
  for (p = res; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == -1) continue;   /* failure, try next socket */

    /* connect even though it is a UDP socket, this sets the defaults
       destination address and allow using send instead of sendto */
    ret = connect(sock, p->ai_addr, p->ai_addrlen);
    if (ret == 0) break;        /* success */

    close(sock);
  }

  freeaddrinfo(res);

  if (p == NULL) {
    margo_error(mid, "%s: Could not open FlexMPI socket", __func__);
    return -1;
  }

  return sock;
}


flexmpi_reconfigure_t
flexmpi_func(margo_instance_id mid, void **handle)
{
  flexmpi_reconfigure_t func;

  *handle = dlopen(LIBEMPI_SO, RTLD_NOW);
  if (!*handle) {
    margo_info(mid, "%s: FlexMPI dlopen %s", __func__, dlerror());
    return NULL;
  }

  func = dlsym(*handle, FLEXMPI_RECONFIGURE);
  if (!func) {
    margo_info(mid, "%s: FlexMPI dlsym %s", __func__, dlerror());
  }

  return func;
}


int
flexmpi_reconfigure(margo_instance_id mid, uint32_t maxprocs,
                    const char *hostlist __attribute__((unused)),
                    flexmpi_reconfigure_t flexmpifunc, int flexmpisock)
{
  ABT_mutex mutex;
  int nbytes;
  char cmd[FLEXMPI_COMMAND_LEN];

  margo_error(mid, "%s: IN FLEXMPI RECONFIGURE FUNCTION", __func__);

  nbytes = snprintf(cmd, FLEXMPI_COMMAND_LEN, "6:lhost:%u", maxprocs);

  if (nbytes < 0 || nbytes >= FLEXMPI_COMMAND_LEN) {
    margo_error(mid, "Error generating FlexMPI command ");
    return -1;
  }

  /* try pointer to FlexMPI reconfiguration function */
  if (flexmpifunc) {
    return flexmpifunc(cmd);
  }

  /* no FlexMPI function pointer, fallback to socket control */
  if (flexmpisock == -1) {
    margo_error(mid, "%s: FlexMPI socket uninitialized", __func__);
    return -1;
  }

  mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutexmem);
  ABT_mutex_lock(mutex);

  /* try to send the FlexMPI command
     if the send blocks, ignore and wait for the next command.
     /!\ Specifically avoid blocking because we are holding a mutex */
  nbytes = send(flexmpisock, cmd, strlen(cmd), MSG_DONTWAIT);

  ABT_mutex_unlock(mutex);

  if (nbytes == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      margo_error(mid, "%s: Could not write to FlexMPI socket (%s)", __func__, strerror(errno));
      return -1;
    }
  }
  return -1;
}
