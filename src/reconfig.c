#include <assert.h>
#include <dlfcn.h>              /* dlopen/dlsym */
#include <errno.h>
#include <netdb.h>              /* sockets */
#include <sys/socket.h>         /* sockets */
#include <sys/types.h>          /* sockets */
#include <unistd.h>             /* close */
#include <margo.h>

#include "rpc.h"
#include "reconfig.h"


/* XX TMP: FlexMPI specific */
#define LIBEMPI_SO "libempi.so"                   /* FlexMPI library */
#define FLEXMPI_RECONFIGURE "flexmpi_reconfigure" /* FlexMPI reconfigure func */
#define FLEXMPI_COMMAND_LEN 256

static int flexmpi_reconfigure(margo_instance_id mid, uint32_t maxprocs, const char *hostlist, flexmpi_reconfigure_t flexmpifunc, int flexmpisock);

static ABT_mutex_memory mutexmem = ABT_MUTEX_INITIALIZER;


void
reconfigure_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  reconfigure_in_t in;
  rpc_out_t out;
  int rc;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = RPC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = RPC_FAILURE;
    margo_error(mid, "Input failure RPC_RECONFIGURE: %s", HG_Error_to_string(hret));
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct reconfig_data *d = (struct reconfig_data *)margo_registered_data(mid, info->id);

  if (!d) {
    margo_error(mid, "RPC_RECONFIG: No reconfiguration data");
    out.rc = RPC_FAILURE;
    goto respond;
  }

  /* call registered function */
  if (d->func) {
    rc = d->func(in.maxprocs, in.hostlist, d->data);
  } else if (d->type == ICC_TYPE_FLEXMPI ) {
    margo_error(mid, "IN RECONFIGURE_CB");
    rc = flexmpi_reconfigure(mid, in.maxprocs, in.hostlist, d->flexmpifunc, d->flexmpisock);
    margo_error(mid, "OUT RECONFIGURE_CB");
  } else {
    rc = RPC_FAILURE;
  }
  out.rc = rc ? RPC_FAILURE : RPC_SUCCESS;

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    margo_error(mid, "Response failure RPC_RECONFIGURE: %s", HG_Error_to_string(hret));
  }
}
DEFINE_MARGO_RPC_HANDLER(reconfigure_cb);


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


static int
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
