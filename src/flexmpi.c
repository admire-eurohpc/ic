#include <assert.h>
#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>
#include <netdb.h>              /* sockets */
#include <sys/socket.h>         /* sockets */
#include <sys/types.h>          /* sockets */
#include <unistd.h>             /* close */
#include <margo.h>

#include "rpc.h"
#include "flexmpi.h"


#define LIBEMPI_SO "libempi.so"                   /* FlexMPI library */
#define FLEXMPI_RECONFIGURE "flexmpi_reconfigure" /* FlexMPI reconfigure func */

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
flexmpi_func(margo_instance_id mid)
{
  void *handle;
  flexmpi_reconfigure_t func;

  handle = dlopen(LIBEMPI_SO, RTLD_NOW);

  if (!handle) {
    margo_info(mid, "%s: FlexMPI %s", __func__, dlerror());
    return NULL;
  }

  func = dlsym(handle, FLEXMPI_RECONFIGURE);

  if (!func) {
    margo_info(mid, "%s: FlexMPI %s", __func__, dlerror());
  }

  return func;
}


void
flexmpi_malleability_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  flexmpi_malleability_in_t in;
  rpc_out_t out;
  ABT_mutex mutex;
  int rc, nbytes;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_OK;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_ERR;
    margo_error(mid, "%s: Error getting FLEXMPI_MALLEABILITY RPC input: %s", __func__, HG_Error_to_string(hret));
    goto respond;
  }

  if (strnlen(in.command, FLEXMPI_COMMAND_MAX_LEN) == FLEXMPI_COMMAND_MAX_LEN) {
    out.rc = RPC_E2BIG;
    goto respond;
  }

  /* get function pointer and socket data */
  const struct hg_info *info = margo_get_info(h);
  struct flexmpi_cbdata *data = (struct flexmpi_cbdata *)margo_registered_data(mid, info->id);

  /* try pointer to FlexMPI reconfiguration function */
  if (data->func) {
    rc = data->func(in.command);
    out.rc = rc ? RPC_ERR : RPC_OK;
    goto respond;
  }

  /* no FlexMPI function pointer, fallback to socket control */
  if (data->sock == -1) {
    margo_error(mid, "%s: FlexMPI socket uninitialized", __func__);
    out.rc = RPC_ERR;
    goto respond;
  }

  mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutexmem);
  ABT_mutex_lock(mutex);

  /* try to send the FlexMPI command
     if the send blocks, ignore and wait for the next command.
     /!\ Specifically avoid blocking because we are holding a mutex */
  nbytes = send(data->sock, in.command, strlen(in.command), MSG_DONTWAIT);

  ABT_mutex_unlock(mutex);

  if (nbytes == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      margo_error(mid, "%s: Could not write to FlexMPI socket (%s)", __func__, strerror(errno));
      out.rc = RPC_ERR;
    }
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "%s: Could not respond to RPC", __func__);
  }
}
DEFINE_MARGO_RPC_HANDLER(flexmpi_malleability_cb);
