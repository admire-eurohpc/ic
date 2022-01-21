#include <assert.h>
#include <inttypes.h>
#include <margo.h>

#include "cbserver.h"
#include "rpc.h"
#include "icdb.h"


#define MARGO_GET_INPUT(h,in,hret)  hret = margo_get_input(h, &in);     \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not get RPC input");                          \
  }

#define MARGO_RESPOND(h,out,hret)  hret = margo_respond(h, &out);       \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not respond to RPC");                         \
  }

#define MARGO_DESTROY_HANDLE(h,hret)  hret = margo_destroy(h);          \
  if (hret != HG_SUCCESS) {                                             \
    LOG_ERROR(mid, "Could not destroy Margo RPC handle: %s", HG_Error_to_string(hret)); \
  }


/* XX temp malleability manager stub */
#define NCLIENTS     4
#define NCLIENTS_MAX 1024
#define TIMEOUT_MS   4000

struct malleability_manager_arg {
  margo_instance_id   mid;
  struct icdb_context **icdbs;
  hg_id_t             *rpcids;
  uint32_t            jobid;
};
static void malleability_manager_th(void *arg);


void
client_register_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  client_register_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    goto respond;
  }

  margo_info(mid, "Registering client %s", in.clid);

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  assert(data != NULL);
  assert(data->icdbs != NULL);

  /* write client to DB */
  ret = icdb_setclient(data->icdbs[xrank], in.clid, in.type, in.addr_str, in.provid, in.jobid, in.jobntasks, in.jobnnodes, in.nprocs);
  if (ret != ICDB_SUCCESS) {
    if (data->icdbs[xrank]) {
      LOG_ERROR(mid, "Could not write client %s to database: %s", in.clid, icdb_errstr(data->icdbs[xrank]));
    }
    out.rc = ICC_FAILURE;
  }

  /* XX malleability */
  /* XX TEMP */
  /* run a separate "malleability" thread taken from the pool of Margo ULTs */
  ABT_pool pool;
  margo_get_handler_pool(mid, &pool);

  /* rpcids & icdbs are allocated in main and can be passed as is to
     malleability thread */
  struct malleability_manager_arg arg = {
    .mid = mid, .rpcids = data->rpcids, .icdbs = data->icdbs, .jobid = in.jobid
  };

  /* XX return code from ULT? */
  ret = ABT_thread_create(pool, malleability_manager_th, &arg, ABT_THREAD_ATTR_NULL, NULL);
  if (ret != 0) {
    LOG_ERROR(mid, "Could not create malleability ULT (ret = %d)", ret);
    out.rc = ICC_FAILURE;
  }


 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(client_register_cb);


void
client_deregister_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  client_deregister_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  margo_info(mid, "Deregistering client %s", in.clid);

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  assert(data != NULL);
  assert(data->icdbs != NULL);

  /* remove client from DB */
  uint32_t jobid;

  ret = icdb_delclient(data->icdbs[xrank], in.clid, &jobid);

  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not delete client %s: %s", __func__, in.clid, icdb_errstr(data->icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

  /* XX malleability */
  /* XX TEMP */
  /* run a separate "malleability" thread taken from the pool of Margo ULTs */
  ABT_pool pool;
  margo_get_handler_pool(mid, &pool);

  /* XX fixme arg WILL GO OUT OF SCOPE */
  struct malleability_manager_arg arg = {
    .mid = mid, .rpcids = data->rpcids, .icdbs = data->icdbs, .jobid = jobid
  };

  /* XX return code from ULT? */
  ret = ABT_thread_create(pool, malleability_manager_th, &arg, ABT_THREAD_ATTR_NULL, NULL);
  if (ret != 0) {
    LOG_ERROR(mid, "Could not create malleability ULT (ret = %d)", ret);
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(client_deregister_cb);


void
jobmon_submit_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  int ret;
  int xrank;
  jobmon_submit_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  assert(data != NULL);
  assert(data->icdbs != NULL);

  margo_info(mid, "Job %"PRIu32".%"PRIu32" started on %"PRIu32" node%s",
             in.jobid, in.jobstepid, in.nnodes, in.nnodes > 1 ? "s" : "");

  ret = icdb_command(data->icdbs[xrank], "SET nnodes:%"PRIu32".%"PRIu32" %"PRIu32,
                     in.jobid, in.jobstepid, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(data->icdbs[xrank]));
  }

 respond:
  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(jobmon_submit_cb);


void
jobmon_exit_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  jobmon_submit_in_t in;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  MARGO_GET_INPUT(h,in,hret);
  if (hret == HG_SUCCESS) {
    margo_info(mid, "Slurm Job %"PRIu32".%"PRIu32" exited", in.jobid, in.jobstepid);
  }

  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(jobmon_exit_cb);

void
adhoc_nodes_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  adhoc_nodes_in_t in;
  rpc_out_t out;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h,in,hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
  } else {
    margo_info(mid, "IC got adhoc_nodes request from job %"PRIu32": %"PRIu32" nodes (%"PRIu32" nodes assigned by Slurm)",
               in.jobid, in.nnodes, in.nnodes);
  }

  MARGO_RESPOND(h, out, hret);
  MARGO_DESTROY_HANDLE(h, hret);
}
DEFINE_MARGO_RPC_HANDLER(adhoc_nodes_cb);


void
malleability_avail_cb(hg_handle_t h)
{
  hg_return_t hret;
  margo_instance_id mid;
  malleability_avail_in_t in;
  rpc_out_t out;
  int ret;
  int xrank;

  mid = margo_hg_handle_get_instance(h);
  assert(mid);

  out.rc = ICC_SUCCESS;

  MARGO_GET_INPUT(h, in, hret);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    margo_error(mid, "%s: Could not get Argobots ES rank", __func__);
    goto respond;
  }

  const struct hg_info *info = margo_get_info(h);
  struct cb_data *data = (struct cb_data *)margo_registered_data(mid, info->id);

  assert(data != NULL);
  assert(data->icdbs != NULL);

  /* store nodes available for malleability in db */
  ret = icdb_command(data->icdbs[xrank], "HMSET malleability_avail:%"PRIu32" type %s portname %s nnodes %"PRIu32,
                     in.jobid, in.type, in.portname, in.nnodes);

  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "%s: Could not write to IC database: %s", __func__, icdb_errstr(data->icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

 respond:
  MARGO_RESPOND(h, out, ret);
  MARGO_DESTROY_HANDLE(h, hret)
}
DEFINE_MARGO_RPC_HANDLER(malleability_avail_cb);


/* Malleability manager stub */

static void
malleability_manager_th(void *arg)
{
  hg_return_t hret;
  margo_instance_id mid;
  hg_id_t *rpcids;
  struct icdb_context **icdbs;
  uint32_t jobid;
  int ret, rpcret;
  int xrank;
  void *tmp;

  mid = ((struct malleability_manager_arg *)arg)->mid;
  rpcids = ((struct malleability_manager_arg *)arg)->rpcids;
  icdbs = ((struct malleability_manager_arg *)arg)->icdbs;
  jobid = ((struct malleability_manager_arg *)arg)->jobid;

  if (!icdbs) {
    LOG_ERROR(mid, "ICDB context is NULL");
    return;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    return;
  }

  size_t size;
  unsigned long long nclients;
  struct icdb_context *icdb;
  struct icdb_job job;
  struct icdb_client *clients;

  icdb = icdbs[xrank];

  ret = icdb_getjob(icdb, jobid, &job);
  if (ret != ICDB_SUCCESS) {
    LOG_ERROR(mid, "IC database error: %s", icdb_errstr(icdb));
    return;
  }

  size = NCLIENTS;
  /* XX fixme: multiplication could overflow, use reallocarray? */
  /* XX do not alloc/free on every call */
  clients = malloc(sizeof(*clients) * size);
  if (!clients) {
    LOG_ERROR(mid, "Failed malloc");
    return;
  }

  do {
    /* XX fixme filter on flexmpi client */
    ret = icdb_getclients(icdb, NULL, 0, clients, size, &nclients);

    /* clients array is too small, expand */
    if (ret == ICDB_E2BIG && size < NCLIENTS_MAX) {
      size *= 2;
      tmp = realloc(clients, sizeof(*clients) * size);
      if (!tmp) {
        LOG_ERROR(mid, "Failed malloc");
        return;
      }
      clients = tmp;
      continue;
    }
    else if (ret != ICDB_SUCCESS) {
      LOG_ERROR(mid, "IC database error: %s", icdb_errstr(icdb));
      free(clients);
      return;
    }
    break;
  } while (1);


  for (unsigned i = 0; i < nclients; i++) {
    char command[FLEXMPI_COMMAND_LEN];
    flexmpi_malleability_in_t in;
    int nbytes;
    long long dprocs;

    dprocs = job.ntasks / nclients - clients[i].nprocs;

    nbytes = snprintf(command, FLEXMPI_COMMAND_LEN, "6:lhost:%lld", dprocs);
    if (nbytes >= FLEXMPI_COMMAND_LEN || nbytes < 0) {
      LOG_ERROR(mid, "Could not prepare malleability command");
      break;
    }

    in.command = command;
    LOG_ERROR(mid, "COMMAND %s", command);

    hg_addr_t addr;

    hret = margo_addr_lookup(mid, clients[i].addr, &addr);
    if (hret != HG_SUCCESS) {
      LOG_ERROR(mid, "Could not get Mercury address: %s", HG_Error_to_string(hret));
      break;
    }

    ret = rpc_send(mid, addr, rpcids[RPC_FLEXMPI_MALLEABILITY], &in, &rpcret);
    if (ret) {
      LOG_ERROR(mid, "Could not send RPC %d", RPC_FLEXMPI_MALLEABILITY);
    } else if (rpcret) {
      LOG_ERROR(mid, "RPC %d returned with code %d", RPC_FLEXMPI_MALLEABILITY, rpcret);
    }
    else {
      /* XX generalize with a "writeclient" function? */
      ret = icdb_incrnprocs(icdb, clients[i].clid, dprocs);
      if (ret != ICDB_SUCCESS) {
        LOG_ERROR(mid, "IC database error: %s", icdb_errstr(icdb));
      }
    }
  }

  free(clients);
  return;
}
