#include <errno.h>
#include <inttypes.h>           /* PRIdxx */
#include <stdarg.h>             /* va_arg */
#include <stdio.h>
#include <margo.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include "icc_rpc.h"
#include "icc.h"
#include "icdb.h"


#define NTHREADS 3
#define TIMEOUT_MS 5000

#define LOG_ERROR(mid,fmt, ...)  margo_error(mid, "%s (%s:%d): "fmt, __func__, __FILE__, __LINE__ __VA_OPT__(,)__VA_ARGS__)

/* XX bad global variable? */
static struct icdb_context *icdbs[NTHREADS] = { NULL };

/* XX temp malleability manager stub */
#define NCLIENTS     4
#define NCLIENTS_MAX 1024

struct malleability_manager_arg {
  margo_instance_id mid;
  hg_id_t *rpc_ids;
};
void malleability_manager_th(void *arg);

/* internal RPCs callbacks */
static void target_register_cb(hg_handle_t h, margo_instance_id mid);
static void target_deregister_cb(hg_handle_t h, margo_instance_id mid);

/* public RPCs callbacks */
static void test_cb(hg_handle_t h, margo_instance_id mid);
static void malleability_avail_cb(hg_handle_t h, margo_instance_id mid);
static void jobmon_submit_cb(hg_handle_t h, margo_instance_id mid);
static void jobmon_exit_cb(hg_handle_t h, margo_instance_id mid);
static void adhoc_nodes_cb(hg_handle_t h, margo_instance_id mid);


int
main(int argc __attribute__((unused)), char** argv __attribute__((unused)))
{
  margo_instance_id mid;
  int rc;

  assert(NTHREADS > 0);

  /* use one ULT for main + network, NTHREADS - 1 ULTs for callbacks */
  mid = margo_init(HG_PROTOCOL, MARGO_SERVER_MODE, 0, NTHREADS - 1);
  if (!mid) {
    LOG_ERROR(mid, "Could not initialize Margo instance with Mercury provider "HG_PROTOCOL);
    goto error;
  }

  margo_set_log_level(mid, MARGO_LOG_INFO);

  hg_size_t addr_str_size = ICC_ADDR_LEN;
  char addr_str[ICC_ADDR_LEN];

  rc = get_hg_addr(mid, addr_str, &addr_str_size);
  if (rc) {
    LOG_ERROR(mid, "Could not get Mercury address");
    goto error;
  }

  margo_info(mid, "Margo Server running at address %s", addr_str);

  /* write Mercury address to file */
  FILE *f;
  int nbytes;
  char *path = icc_addr_file();

  if (!path) {
    LOG_ERROR(mid, "Could not get ICC address file");
    goto error;
  }

  f = fopen(path, "w");
  if (f == NULL) {
    LOG_ERROR(mid, "Could not open ICC address file \"%s\": %s", path, strerror(errno));
    free(path);
    goto error;
  }
  free(path);

  nbytes = fprintf(f, "%s", addr_str);
  if (nbytes < 0 || (unsigned)nbytes != addr_str_size - 1) {
    LOG_ERROR(mid, "Could not write to address file: %s", strerror(errno));
    fclose(f);
    goto error;
  }
  fclose(f);

  /* register RPCs */
  hg_id_t rpc_ids[ICC_RPC_COUNT] = { 0 };             /* RPC id table */
  icc_callback_t callbacks[ICC_RPC_COUNT] = { NULL }; /* callback table */

  hg_bool_t flag;
  /* XX fix test */
  margo_provider_registered_name(mid, "icc_test", MARGO_PROVIDER_DEFAULT, &rpc_ids[ICC_RPC_COUNT], &flag);
  if(flag == HG_TRUE) {
    LOG_ERROR(mid, "Provider %d already exists", MARGO_PROVIDER_DEFAULT);
    goto error;
  }

  /* RPCs to receive */
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_TARGET_REGISTER, target_register_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_TARGET_DEREGISTER, target_deregister_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_TEST, test_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_JOBMON_SUBMIT, jobmon_submit_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_JOBMON_EXIT, jobmon_exit_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_ADHOC_NODES, adhoc_nodes_cb);
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_MALLEABILITY_AVAIL, malleability_avail_cb);

  /* RPCs to send */
  REGISTER_PREP(rpc_ids, callbacks, ICC_RPC_FLEXMPI_MALLEABILITY, NULL);

  rc = register_rpcs(mid, callbacks, rpc_ids);
  if (rc) {
    LOG_ERROR(mid, "Could not register RPCs");
    goto error;
  }

  /* initialize connection to DB. Because the icdb_context is not
     thread safe, create one per OS threads (Argobots "execution
     stream") */
  for (size_t i = 0; i < NTHREADS; i++) {
    rc = icdb_init(&icdbs[i]);
    if (!icdbs[i]) {
      LOG_ERROR(mid, "Could not initialize IC database");
      goto error;
    } else if (rc != ICDB_SUCCESS) {
      LOG_ERROR(mid, "Could not initialize IC database: %s", icdb_errstr(icdbs[i]));
      goto error;
    }
  }

  /* XX TEMP */
  /* run a separate "malleability" thread taken from the pool of Margo ULTs */
  ABT_pool pool;
  margo_get_handler_pool(mid, &pool);

  struct malleability_manager_arg arg = { .mid = mid, .rpc_ids = rpc_ids };

  /* XX return code from ULT? */
  rc = ABT_thread_create(pool, malleability_manager_th, &arg, ABT_THREAD_ATTR_NULL, NULL);
  if (rc != 0) {
    LOG_ERROR(mid, "Could not create malleability ULT (ret = %d)", rc);
    goto error;
  }

  margo_wait_for_finalize(mid);

  /* close connections to DB */
  for (size_t i = 0; i < NTHREADS; i++) {
    icdb_fini(&icdbs[i]);
  }

  return 0;

 error:
  if (mid) margo_finalize(mid);
  return -1;
}


/* RPC callbacks */
static void
target_register_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  int ret;
  int xrank;                    /* execution stream id */
  target_register_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    goto respond;
  }

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input");
    goto respond;
  }

  margo_info(mid, "Registering client %s", in.clid);

  ret = icdb_setclient(icdbs[xrank], in.clid, in.type, in.addr_str, in.provid, in.jobid);
  if (ret != ICDB_SUCCESS) {
    if (icdbs[xrank]) {
      LOG_ERROR(mid, "Could not write to IC database: %s", icdb_errstr(icdbs[xrank]));
    }
    out.rc = ICC_FAILURE;
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    /* XX prefix log with __func__ */
    LOG_ERROR(mid, "Could not respond to RPC");
  }
}


static void
target_deregister_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  int ret;
  int xrank;

  target_deregister_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input");
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    goto respond;
  }

  margo_info(mid, "Deregistering client %s", in.clid);

  ret = icdb_delclient(icdbs[xrank], in.clid);
  if (ret != ICDB_SUCCESS) {
    LOG_ERROR(mid, "Could not delete client %s: %s", in.clid, icdb_errstr(icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to RPC");
  }
}


static void
test_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  test_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
  } else {
    margo_info(mid, "Got \"TEST\" RPC from client %s with argument %u\n", in.clid, in.number);
  }

  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to HPC");
  }
}


static void
malleability_avail_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  malleability_avail_in_t in;
  rpc_out_t out;
  int ret;
  int xrank;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input: %s", HG_Error_to_string(hret));
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    goto respond;
  }

  /* store nodes available for malleability in db */
  ret = icdb_command(icdbs[xrank], "HMSET malleability_avail:%"PRIu32" type %s portname %s nnodes %"PRIu32,
                      in.jobid, in.type, in.portname, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    LOG_ERROR(mid, "Could not write to IC database: %s", icdb_errstr(icdbs[xrank]));
    out.rc = ICC_FAILURE;
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to HPC");
  }
}


static void
jobmon_submit_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;
  int ret;
  int xrank;

  jobmon_submit_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input");
    goto respond;
  }

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    goto respond;
  }

  margo_info(mid, "Job %"PRIu32".%"PRIu32" started on %"PRIu32" node%s",
             in.jobid, in.jobstepid, in.nnodes, in.nnodes > 1 ? "s" : "");

  ret = icdb_command(icdbs[xrank], "SET nnodes:%"PRIu32".%"PRIu32" %"PRIu32,
                     in.jobid, in.jobstepid, in.nnodes);
  if (ret != ICDB_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not write to IC database: %s", icdb_errstr(icdbs[xrank]));
  }

 respond:
  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to HPC");
  }
}


static void
jobmon_exit_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;

  jobmon_submit_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input");
  }

  margo_info(mid, "Slurm Job %"PRIu32".%"PRIu32" exited", in.jobid, in.jobstepid);

  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to HPC");
  }
}


static void
adhoc_nodes_cb(hg_handle_t h, margo_instance_id mid)
{
  hg_return_t hret;

  adhoc_nodes_in_t in;
  rpc_out_t out;

  out.rc = ICC_SUCCESS;

  hret = margo_get_input(h, &in);
  if (hret != HG_SUCCESS) {
    out.rc = ICC_FAILURE;
    LOG_ERROR(mid, "Could not get RPC input");
  }

  margo_info(mid, "IC got adhoc_nodes request from job %"PRIu32": %"PRIu32" nodes (%"PRIu32" nodes assigned by Slurm)",
             in.jobid, in.nnodes, in.nnodes);

  hret = margo_respond(h, &out);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "Could not respond to HPC");
  }
}


/* Malleability manager stub */
char *instr_vec[6];             /* vector of malleability instructions */
char *next_instruction = NULL;  /* next malleability instruction.
                                   SHARED between MM_th and IC */
void
malleability_manager_th(void *arg)
{
  hg_return_t hret;
  hg_addr_t addr;
  hg_id_t *rpcids;
  margo_instance_id mid;
  int ret, rpcret;
  int xrank;
  void *tmp;

  mid = ((struct malleability_manager_arg *)arg)->mid;
  rpcids = ((struct malleability_manager_arg *)arg)->rpc_ids;

  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not get Argobots ES rank");
    return;
  }

  size_t size;
  unsigned long long nclients;
  struct icdb_client *clients;

  size = NCLIENTS;
  /* XX multiplication could overflow, use reallocarray? */
  clients = malloc(sizeof(*clients) * size);
  if (!clients) {
    LOG_ERROR(mid, "Failed malloc");
    return;
  }

  /* XX hardcoded malleability commands */
  size_t cmidx = 0;
  char *commands[] = {
    "6:compute-12-2:2",
    "6:compute-12-3:4",
    "6:compute-12-3:-4",
    "6:compute-12-2:-2",
    "5"
  };

  while(1) {
    do {
      ret = icdb_getclients(icdbs[xrank], NULL, 0, clients, size, &nclients);

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
        LOG_ERROR(mid, "IC database error: %s", icdb_errstr(icdbs[xrank]));
        free(clients);
        return;
      }
      break;
    } while (1);


    flexmpi_malleability_in_t in;

    for (unsigned i = 0; i < nclients; i++) {
      hret = margo_addr_lookup(mid, clients[i].addr, &addr);
      if (hret != HG_SUCCESS) {
        LOG_ERROR(mid, "Could not get Mercury address: %s", HG_Error_to_string(hret));
      }

      in.command = commands[cmidx];

      ret = rpc_send(mid, addr, clients[i].provid, rpcids[ICC_RPC_FLEXMPI_MALLEABILITY], &in, &rpcret);
      if (ret) {
        LOG_ERROR(mid, "Could not send RPC %d", ICC_RPC_FLEXMPI_MALLEABILITY);
      } else if (rpcret) {
        LOG_ERROR(mid, "RPC %d returned with error %d", rpcret);
      }
    }
    cmidx = (cmidx + 1) % 5 ;            /* send next command */
    margo_thread_sleep(mid, TIMEOUT_MS); /* sleep and retry clients query */
  }

  free(clients);
  return;
}
