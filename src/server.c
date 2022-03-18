#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIdxx */
#include <margo.h>

#include "rpc.h"
#include "icc.h"
#include "icdb.h"
#include "icrm.h"
#include "cbcommon.h"
#include "cbserver.h"


#define NTHREADS 8              /* threads set aside for RPC handling */

/* malleability manager stub */
#define NCLIENTS     4
#define NCLIENTS_MAX 1024

static void malleability_th(void *arg);


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
  rpc_ids[RPC_JOBCLEAN] = MARGO_REGISTER(mid, RPC_JOBCLEAN_NAME, jobclean_in_t, rpc_out_t, jobclean_cb);
  rpc_ids[RPC_JOBMON_SUBMIT] = MARGO_REGISTER(mid, RPC_JOBMON_SUBMIT_NAME, jobmon_submit_in_t, rpc_out_t, jobmon_submit_cb);
  rpc_ids[RPC_JOBMON_EXIT] = MARGO_REGISTER(mid, RPC_JOBMON_EXIT_NAME, jobmon_exit_in_t, rpc_out_t, jobmon_exit_cb);
  rpc_ids[RPC_ADHOC_NODES] = MARGO_REGISTER(mid, RPC_ADHOC_NODES_NAME, adhoc_nodes_in_t, rpc_out_t, adhoc_nodes_cb);
  rpc_ids[RPC_RESALLOC] = MARGO_REGISTER(mid, RPC_RESALLOC_NAME, resalloc_in_t, rpc_out_t, NULL);
  rpc_ids[RPC_RESALLOCDONE] = MARGO_REGISTER(mid, RPC_RESALLOCDONE_NAME, resallocdone_in_t, rpc_out_t, resallocdone_cb);
  rpc_ids[RPC_RECONFIGURE] = MARGO_REGISTER(mid, RPC_RECONFIGURE_NAME, reconfigure_in_t, rpc_out_t, NULL);
  rpc_ids[RPC_MALLEABILITY_AVAIL] = MARGO_REGISTER(mid, RPC_MALLEABILITY_AVAIL_NAME, malleability_avail_in_t, rpc_out_t, malleability_avail_cb);
  rpc_ids[RPC_MALLEABILITY_REGION] = MARGO_REGISTER(mid, RPC_MALLEABILITY_REGION_NAME, malleability_region_in_t, rpc_out_t, malleability_region_cb);

  ABT_pool rpc_pool;
  margo_get_handler_pool(mid, &rpc_pool);

  /* malleability thread from the pool of Margo ULTs */
  struct malleability_data malldat;

  ABT_mutex_create(&(malldat.mutex));
  ABT_cond_create(&(malldat.cond));
  malldat.sleep = 1;

  malldat.mid = mid;
  malldat.rpcids = rpc_ids;
  malldat.icdbs = icdbs;
  malldat.jobid = 0;

  rc = ABT_thread_create(rpc_pool, malleability_th, &malldat, ABT_THREAD_ATTR_NULL, NULL);
  if (rc != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not create malleability ULT (ret = %d)", rc);
    goto error;
  }

  /* attach various pieces of data to RPCs  */
  struct cb_data d = { .icdbs = icdbs, .rpcids = rpc_ids, .malldat = &malldat };
  margo_register_data(mid, rpc_ids[RPC_CLIENT_REGISTER], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_CLIENT_DEREGISTER], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_JOBCLEAN], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_JOBMON_SUBMIT], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_MALLEABILITY_AVAIL], &d, NULL);

  margo_wait_for_finalize(mid);

  /* clean up malleability thread */
  ABT_mutex_free(&malldat.mutex);
  ABT_cond_free(&malldat.cond);

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

void
malleability_th(void *arg)
{
  struct icdb_client *clients;
  struct malleability_data *data = (struct malleability_data *)arg;

  while (1) {
    ABT_mutex_lock(data->mutex);
    while (data->sleep)
      ABT_cond_wait(data->cond, data->mutex);
    ABT_mutex_unlock(data->mutex);

    int ret, xrank;
    void *tmp;

    if (!data->icdbs) {
      LOG_ERROR(data->mid, "ICDB context is NULL");
      return;
    }

    ret = ABT_self_get_xstream_rank(&xrank);
    if (ret != ABT_SUCCESS) {
      LOG_ERROR(data->mid, "Argobots ES rank");
      return;
    }

    size_t nclients;
    struct icdb_context *icdb;
    struct icdb_job job;

    icdb = data->icdbs[xrank];

    ret = icdb_getjob(icdb, data->jobid, &job);
    if (ret != ICDB_SUCCESS) {
      LOG_ERROR(data->mid, "IC database: %s", icdb_errstr(icdb));
      return;
    }

    nclients = NCLIENTS;
    /* XX fixme: multiplication could overflow, use reallocarray? */
    /* XX do not alloc/free on every call */
    clients = malloc(sizeof(*clients) * nclients);
    if (!clients) {
      LOG_ERROR(data->mid, "Failed malloc");
      return;
    }

    do {
      /* XX fixme filter on (flex)MPI clients?*/
      ret = icdb_getclients(icdb, NULL, data->jobid, clients, &nclients);

      /* clients array is too small, expand */
      if (ret == ICDB_E2BIG && nclients <= NCLIENTS_MAX) {
        tmp = realloc(clients, sizeof(*clients) * nclients);
        if (!tmp) {
          LOG_ERROR(data->mid, "Failed malloc");
          return;
        }
        clients = tmp;
        continue;
      }
      else if (nclients > NCLIENTS_MAX){
        LOG_ERROR(data->mid, "Too many clients returned from DB");
        return;
      }
      else if (ret != ICDB_SUCCESS) {
        LOG_ERROR(data->mid, "IC database: %s", icdb_errstr(icdb));
        free(clients);
        return;
      }
      break;
    } while (1);


    margo_info(data->mid, "Malleability: Job %"PRIu32": got %zu client%s",
               data->jobid, nclients, nclients > 1 ? "s" : "");

    for (size_t i = 0; i < nclients; i++) {
      long long dprocs;

      dprocs = job.ntasks / nclients - clients[i].nprocs;

      if (dprocs < INT32_MIN || dprocs > INT32_MAX) {
	LOG_ERROR(data->mid, "Reconfiguration: Job %"PRIu32": too many new processes");
	break;
      }

      margo_info(data->mid, "Malleability: Job %"PRIu32": client %s: %s%"PRId32" procs", clients[i].jobid, clients[i].clid, dprocs > 0 ? "+" : "", dprocs);

      /* make malleability RPC */
      hg_addr_t addr;
      hg_return_t hret;
      int rpcret;

      /* XX number reconfiguration command, add hostlist */
      reconfigure_in_t in = { .cmdidx = 0, .maxprocs = dprocs, .hostlist = "" };

      hret = margo_addr_lookup(data->mid, clients[i].addr, &addr);
      if (hret != HG_SUCCESS) {
        LOG_ERROR(data->mid, "Failed getting Mercury address: %s", HG_Error_to_string(hret));
        break;
      }

      /* XX TMP test alterjob */
      resalloc_in_t allocin;
      allocin.shrink = 0;
      allocin.nnodes = 4;

      ret = rpc_send(data->mid, addr, data->rpcids[RPC_RESALLOC], &allocin, &rpcret);
      if (ret) {
        LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC send failed ", clients[i].jobid, clients[i].clid);
      } else if (rpcret) {
        LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
      }
      data->sleep = 1;
      continue;

      ret = rpc_send(data->mid, addr, data->rpcids[RPC_RECONFIGURE], &in, &rpcret);
      if (ret) {
        LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RECONFIGURE send failed ", clients[i].jobid, clients[i].clid);
      } else if (rpcret) {
        LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RECONFIGURE returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
      }
      else {
        /* XX generalize with a "writeclient" function? */
        ret = icdb_incrnprocs(icdb, clients[i].clid, dprocs);
        if (ret != ICDB_SUCCESS) {
          LOG_ERROR(data->mid, "IC database failure: %s", icdb_errstr(icdb));
        }
      }
    }

    /* go back to sleep */
    data->sleep = 1;
  }

  free(clients);
  return;
}
