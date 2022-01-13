#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIdxx */
#include <margo.h>

#include "rpc.h"
#include "icc.h"
#include "icdb.h"
#include "cb.h"
#include "cbserver.h"


#define NTHREADS 3
#define TIMEOUT_MS 5000

#define LOG_ERROR(mid,fmt, ...)  margo_error(mid, "%s (%s:%d): "fmt, __func__, __FILE__, __LINE__, ##__VA_ARGS__)

/* XX temp malleability manager stub */
#define NCLIENTS     4
#define NCLIENTS_MAX 1024

struct malleability_manager_arg {
  margo_instance_id mid;
  hg_id_t *rpc_ids;
  struct icdb_context **icdbs;  /* pool of DB connection */
};
static void malleability_manager_th(void *arg);


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
  hg_id_t rpc_ids[RPC_COUNT] = { 0 };             /* RPC id table */
  hg_bool_t flag;

  margo_registered_name(mid, RPC_TEST_NAME, &rpc_ids[RPC_TEST], &flag);
  if(flag == HG_TRUE) {
    LOG_ERROR(mid, "Server error: RPCs already registered");
    goto error;
  }

  /* initialize connections pool to DB. Because the icdb_context is
     not thread safe, we create one connection per OS threads
     (Argobots "execution stream") */
  struct icdb_context *icdbs[NTHREADS] = { NULL };

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

  /* register Margo RPCs */
  rpc_ids[RPC_CLIENT_REGISTER] = MARGO_REGISTER(mid, RPC_CLIENT_REGISTER_NAME, client_register_in_t, rpc_out_t, client_register_cb);
  rpc_ids[RPC_CLIENT_DEREGISTER] = MARGO_REGISTER(mid, RPC_CLIENT_DEREGISTER_NAME, client_deregister_in_t, rpc_out_t, client_deregister_cb);
  rpc_ids[RPC_TEST] = MARGO_REGISTER(mid, RPC_TEST_NAME, test_in_t, rpc_out_t, test_cb);
  rpc_ids[RPC_JOBMON_SUBMIT] = MARGO_REGISTER(mid, RPC_JOBMON_SUBMIT_NAME, jobmon_submit_in_t, rpc_out_t, jobmon_submit_cb);
  rpc_ids[RPC_JOBMON_EXIT] = MARGO_REGISTER(mid, RPC_JOBMON_EXIT_NAME, jobmon_exit_in_t, rpc_out_t, jobmon_exit_cb);
  rpc_ids[RPC_ADHOC_NODES] = MARGO_REGISTER(mid, RPC_ADHOC_NODES_NAME, adhoc_nodes_in_t, rpc_out_t, adhoc_nodes_cb);
  rpc_ids[RPC_MALLEABILITY_AVAIL] = MARGO_REGISTER(mid, RPC_MALLEABILITY_AVAIL_NAME, malleability_avail_in_t, rpc_out_t, malleability_avail_cb);
  rpc_ids[RPC_FLEXMPI_MALLEABILITY] = MARGO_REGISTER(mid, RPC_FLEXMPI_MALLEABILITY_NAME, flexmpi_malleability_in_t, rpc_out_t, NULL);

  /* some server callbacks need access to the pool of db connection */
  margo_register_data(mid, rpc_ids[RPC_CLIENT_REGISTER], icdbs, NULL);
  margo_register_data(mid, rpc_ids[RPC_CLIENT_DEREGISTER], icdbs, NULL);
  margo_register_data(mid, rpc_ids[RPC_JOBMON_SUBMIT], icdbs, NULL);
  margo_register_data(mid, rpc_ids[RPC_MALLEABILITY_AVAIL], icdbs, NULL);

  /* XX TEMP */
  /* run a separate "malleability" thread taken from the pool of Margo ULTs */
  ABT_pool pool;
  margo_get_handler_pool(mid, &pool);

  struct malleability_manager_arg arg = {
    .mid = mid, .rpc_ids = rpc_ids, .icdbs = icdbs
  };

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


/* Malleability manager stub */

char *instr_vec[6];             /* vector of malleability instructions */
char *next_instruction = NULL;  /* next malleability instruction.
                                   SHARED between MM_th and IC */
static void
malleability_manager_th(void *arg)
{
  hg_return_t hret;
  hg_addr_t addr;
  margo_instance_id mid;
  hg_id_t *rpcids;
  struct icdb_context **icdbs;
  int ret, rpcret;
  int xrank;
  void *tmp;

  mid = ((struct malleability_manager_arg *)arg)->mid;
  rpcids = ((struct malleability_manager_arg *)arg)->rpc_ids;
  icdbs = ((struct malleability_manager_arg *)arg)->icdbs;

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
    "6:lhost:2",
    "6:lhost:-2",
    "5:"
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

      ret = rpc_send(mid, addr, rpcids[RPC_FLEXMPI_MALLEABILITY], &in, &rpcret);
      if (ret) {
        LOG_ERROR(mid, "Could not send RPC %d", RPC_FLEXMPI_MALLEABILITY);
      } else if (rpcret) {
        LOG_ERROR(mid, "RPC %d returned with code %d", RPC_FLEXMPI_MALLEABILITY, rpcret);
      }
    }
    if (nclients) {
      cmidx = (cmidx + 1) % (sizeof(commands) / sizeof(commands[0])); /* send next command */
    }
    margo_thread_sleep(mid, TIMEOUT_MS); /* sleep and retry clients query */
  }

  free(clients);
  return;
}
