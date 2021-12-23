#include <errno.h>
#include <netdb.h>              /* sockets */
#include <sys/socket.h>         /* sockets */
#include <sys/types.h>          /* sockets */
#include <margo.h>

#include "flexmpi.h"
#include "rpc.h"

static ABT_mutex_memory mutexmem = ABT_MUTEX_INITIALIZER;

/* XX get rid of global var */
static int sock = 0;                      /* FlexMPI socket */
static struct addrinfo *flexinfo = NULL;  /* FlexMPI addrinfo */

int
flexmpi_socket(margo_instance_id mid, const char *node, const char *service)
{
  /* node = "compute-12-2", service = "7670" */
  struct addrinfo hints, *res, *p;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;    /* allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* datagram socket */

  ret = getaddrinfo(node, service, &hints, &res);
  if (ret != 0) {
    margo_error(mid, "%s: getaddrinfo failed: %s", __func__, gai_strerror(ret));
    return -1;
  }

  /* get first valid socket */
  for (p = res; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == -1) continue;
    break;
  }

  flexinfo = p;

  if (p == NULL) {
    margo_error(mid, "%s: Could not open FlexMPI socket", __func__);
    return -1;
  }

  return sock;
}


void
flexmpi_malleability_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  flexmpi_malleability_in_t in;
  rpc_out_t out;
  ABT_mutex mutex;
  int nbytes;

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


  if (sock == -1 || flexinfo == NULL) {
    margo_error(mid, "%s: FlexMPI socket uninitialized", __func__);
    out.rc = RPC_ERR;
    goto respond;
  }

  mutex = ABT_MUTEX_MEMORY_GET_HANDLE(&mutexmem);
  ABT_mutex_lock(mutex);

  /* try to send the FlexMPI command
     if the send blocks, ignore and wait for the next command.
     /!\ Specifically avoid blocking because we are holding a mutex */
  nbytes = sendto(sock, in.command, strlen(in.command), MSG_DONTWAIT,
                  (struct sockaddr *)flexinfo->ai_addr, flexinfo->ai_addrlen);

  ABT_mutex_unlock(mutex);

  if (nbytes == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      margo_error(mid, "%s: Could not write to FlexMPI socket", __func__);
      out.rc = RPC_ERR;
    }
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "%s: Could not respond to RPC", __func__);
  }
}
