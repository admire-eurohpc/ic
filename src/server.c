#include <assert.h>
#include <errno.h>
#include <inttypes.h>           /* PRIdxx */
#include <unistd.h>             /* sleep */
#include <margo.h>

#include "rpc.h"
#include "icc.h"
#include "icdb.h"
#include "icrm.h"
#include "cbcommon.h"
#include "cbserver.h"

#define NTHREADS 10              /* threads set aside for RPC handling */

/* malleability manager stub */
#define NCLIENTS     4
#define NCLIENTS_MAX 1024
#define IOSET_OUTFILE "iosets_out.csv"

static void malleability_th(void *arg);

/* message stream */
struct mstream {
  margo_instance_id   mid;
  struct icdb_context **icdbs;  /* DB connection pool */
  hg_id_t             *rpcids;  /* RPC handles */
};
static void mstream_th(void *arg);


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

  margo_set_log_level(mid, MARGO_LOG_DEBUG);

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

  /* initialize connection to the resource manager */
  icrm_init();

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
  rpc_ids[RPC_RECONFIGURE2] = MARGO_REGISTER(mid, RPC_RECONFIGURE2_NAME, reconfigure_in_t, rpc_out_t, NULL);
  rpc_ids[RPC_MALLEABILITY_AVAIL] = MARGO_REGISTER(mid, RPC_MALLEABILITY_AVAIL_NAME, malleability_avail_in_t, rpc_out_t, malleability_avail_cb);
  rpc_ids[RPC_MALLEABILITY_REGION] = MARGO_REGISTER(mid, RPC_MALLEABILITY_REGION_NAME, malleability_region_in_t, rpc_out_t, malleability_region_cb);
  rpc_ids[RPC_HINT_IO_BEGIN] = MARGO_REGISTER(mid, RPC_HINT_IO_BEGIN_NAME, hint_io_in_t, hint_io_out_t, hint_io_begin_cb);
  rpc_ids[RPC_LOWMEM] = MARGO_REGISTER(mid, RPC_LOWMEM_NAME, lowmem_in_t, rpc_out_t, NULL);
  rpc_ids[RPC_ALERT] = MARGO_REGISTER(mid, RPC_ALERT_NAME, alert_in_t, rpc_out_t, alert_cb);
  rpc_ids[RPC_NODEALERT] = MARGO_REGISTER(mid, RPC_NODEALERT_NAME, nodealert_in_t, rpc_out_t, nodealert_cb);
  rpc_ids[RPC_METRIC_ALERT] = MARGO_REGISTER(mid, RPC_METRIC_ALERT_NAME, metricalert_in_t, rpc_out_t, metricalert_cb);

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

  /* Message stream thread, from the Margo pool*/
  struct mstream msd = {
    .mid = mid,
    .icdbs = icdbs,
    .rpcids = rpc_ids,
  };
  rc = ABT_thread_create(rpc_pool, mstream_th, &msd, ABT_THREAD_ATTR_NULL, NULL);
  if (rc != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not create message stream ULT (ret = %d)", rc);
    goto error;
  }

  /* attach various pieces of data to RPCs  */
  struct cb_data d = {
    .icdbs = icdbs,
    .rpcids = rpc_ids,
    .malldat = &malldat
  };

  /* iosets data */
  ABT_mutex_create(&d.iosetlock);
  ABT_cond_create(&d.iosetq);
  d.ioset_isrunning = 0;

  ABT_rwlock_create(&d.iosets_lock);
  if (rc != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not create IO-set lock");
    goto error;
  }
  d.iosets = hm_create();
  if (!d.iosets) {
    LOG_ERROR(mid, "Could not create IO-set map");
    goto error;
  }

  ABT_rwlock_create(&d.ioset_time_lock);
  if (rc != ABT_SUCCESS) {
    LOG_ERROR(mid, "Could not create IO-set lock");
    goto error;
  }
  d.ioset_time = hm_create();
  if (!d.ioset_time) {
    LOG_ERROR(mid, "Could not create IO-set timing map");
    goto error;
  }

  d.ioset_outfile = fopen(IOSET_OUTFILE, "w+");
  if (!d.ioset_outfile) {
    LOG_ERROR(mid, "fopen \"%s\" fail: %s", IOSET_OUTFILE, strerror(errno));
    goto error;
  }
  if (fputs("\"appid\",witer,waitstart,iostart,ioend,nbytes\n", d.ioset_outfile) == EOF) {
    LOG_ERROR(mid, "fputs \"%s\" fail", IOSET_OUTFILE);
    goto error;
  }

  margo_register_data(mid, rpc_ids[RPC_CLIENT_REGISTER], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_CLIENT_DEREGISTER], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_JOBCLEAN], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_JOBMON_SUBMIT], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_MALLEABILITY_AVAIL], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_HINT_IO_BEGIN], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_HINT_IO_END], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_ALERT], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_NODEALERT], &d, NULL);
  margo_register_data(mid, rpc_ids[RPC_METRIC_ALERT], &d, NULL);

  margo_wait_for_finalize(mid);

  /* clean up malleability thread */
  ABT_mutex_free(&malldat.mutex);
  ABT_cond_free(&malldat.cond);

  /* clean resource manager connection */
  icrm_fini();

  /* close connections to DB */
  for (size_t i = 0; i < NTHREADS; i++) {
    icdb_fini(&icdbs[i]);
  }

  /* clean up ioset data */
  ABT_cond_free(&d.iosetq);
  ABT_mutex_free(&d.iosetlock);
  ABT_rwlock_free(&d.iosets_lock);
  ABT_rwlock_free(&d.ioset_time_lock);

  /* free the IO-set map */
  const char *setid;
  struct ioset *const *set;
  size_t curs = 0;
  while ((curs = hm_next(d.iosets, curs, &setid, (const void **)&set)) != 0) {
    free(*set);
  }
  hm_free(d.iosets);

  const char *appid;
  struct ioset_time *const *time;
  curs = 0;
  while ((curs = hm_next(d.ioset_time, curs, &appid, (const void **)&time)) != 0) {
    free(*time);
  }
  hm_free(d.ioset_time);
  fclose(d.ioset_outfile);

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
      LOG_ERROR(data->mid, "Null ICDB context");
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
    struct icdb_job *j = &job;
    icdb_job_init(j);

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
      ret = icdb_getclients(icdb, data->jobid, clients, &nclients);

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

    /* reconfigure to share cpus fairly between all steps of a job */
    for (size_t i = 0; i < nclients; i++) {
      /* make malleability RPC */
      hg_addr_t addr;
      hg_return_t hret;
      int rpcret;

      hret = margo_addr_lookup(data->mid, clients[i].addr, &addr);
      if (hret != HG_SUCCESS) {
        LOG_ERROR(data->mid, "Failed getting Mercury address: %s", HG_Error_to_string(hret));
        break;
      }

      /* XX Disable FlexMPI intra node stuff, delete at some point */
      if (0 && strncmp(clients[i].type, "flexmpi", ICC_TYPE_LEN) == 0) {
        long long dprocs = job.ncpus / nclients - clients[i].nprocs;

        if (dprocs < INT32_MIN || dprocs > INT32_MAX) {
          LOG_ERROR(data->mid, "Reconfiguration: Job %"PRIu32": too many new processes");
          break;
        }

        /* XX number reconfiguration command, add hostlist */
        reconfigure_in_t in = { .cmdidx = 0, .maxprocs = dprocs, .hostlist = "" };

        ret = rpc_send(data->mid, addr, data->rpcids[RPC_RECONFIGURE], &in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
        if (ret) {
          LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RECONFIGURE send failed ", clients[i].jobid, clients[i].clid);
        } else if (rpcret) {
          LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RECONFIGURE returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
        }
        else {
          margo_info(data->mid, "Malleability: Job %"PRIu32": client %s: %s%"PRId32" procs", clients[i].jobid, clients[i].clid, dprocs > 0 ? "+" : "", dprocs);
          /* XX generalize with a "writeclient" function? */
          ret = icdb_incrnprocs(icdb, clients[i].clid, dprocs);
          if (ret != ICDB_SUCCESS) {
            LOG_ERROR(data->mid, "IC database failure: %s", icdb_errstr(icdb));
          }
        }
      }

      if (strncmp(clients[i].type, "flexmpi", ICC_TYPE_LEN) ==  0) {

          /* XX TMP test resalloc */
          resalloc_in_t allocin;
          allocin.shrink = 0;
          allocin.ncpus = 6;

          sleep(4);

          ret = rpc_send(data->mid, addr, data->rpcids[RPC_RESALLOC], &allocin, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
          if (ret) {
            LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC send failed ", clients[i].jobid, clients[i].clid);
          } else if (rpcret) {
            LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
          } else {
            margo_info(data->mid, "Malleability: Job %"PRIu32" RPC_RESALLOC for %"PRIu32" CPUs", clients[i].jobid, allocin.ncpus);
          }

          sleep(16);

          allocin.shrink = 1;
          ret = rpc_send(data->mid, addr, data->rpcids[RPC_RESALLOC], &allocin, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
          if (ret) {
            LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC send failed ", clients[i].jobid, clients[i].clid);
          } else if (rpcret) {
            LOG_ERROR(data->mid, "Malleability: Job %"PRIu32": client %s: RPC_RESALLOC returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
          } else {
            margo_info(data->mid, "Malleability: Job %"PRIu32" RPC_RESALLOC for -%"PRIu32" CPUs", clients[i].jobid, allocin.ncpus);
          }
        }

      if (strncmp(clients[i].type, "reconfig2", ICC_TYPE_LEN) ==  0) {
        reconfigure_in_t in = { .cmdidx = 0, .maxprocs = 0, .hostlist = clients[i].nodelist };

        ret = rpc_send(data->mid, addr, data->rpcids[RPC_RECONFIGURE2], &in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
        if (ret) {
          LOG_ERROR(data->mid, "malleability: job %"PRIu32": client %s: RPC_RECONFIGURE2 send failed ", clients[i].jobid, clients[i].clid);
        } else if (rpcret) {
          LOG_ERROR(data->mid, "malleability: job %"PRIu32": client %s: RPC_RECONFIGURE2 returned with code %d", clients[i].jobid, clients[i].clid, rpcret);
        } else {
          margo_info(data->mid, "malleability: job %"PRIu32" RPC_RECONFIG2", clients[i].jobid, clients[i].nodelist);
        }
      }

      /* why is there no mutex here?? */
      data->sleep = 1;
      continue;
    }

	icdb_job_free(&j);

    /* go back to sleep */
    data->sleep = 1;
  }

  free(clients);
  return;
}

void
mall_shrink(margo_instance_id mid, hg_id_t rpcs[], struct icdb_context *icdb) {
  struct icdb_client c;
  int ret, rpcret;

  ret = icdb_getlargestclient(icdb, &c);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "mall: icdb getlargest: %s", icdb_errstr(icdb));
    return;
  }

  char *newnodelist;
  ret = icdb_shrink(icdb, c.clid, &newnodelist);
  if (ret != ICDB_SUCCESS) {
    margo_error(mid, "mall: icdb shrink: %s", icdb_errstr(icdb));
    return;
  }

  hg_addr_t addr;
  hg_return_t hret;
  hret = margo_addr_lookup(mid, c.addr, &addr);
  if (hret != HG_SUCCESS) {
    LOG_ERROR(mid, "hg address: %s", HG_Error_to_string(hret));
    return;
  }

  reconfigure_in_t in = { .cmdidx = 0, .maxprocs = 0, .hostlist = newnodelist };

  ret = rpc_send(mid, addr, rpcs[RPC_RECONFIGURE2], &in, &rpcret, RPC_TIMEOUT_MS_DEFAULT);
  if (ret) {
    LOG_ERROR(mid, "mall: client %s: RPC_RECONFIGURE2 send failed ", c.clid);
  } else if (rpcret) {
    LOG_ERROR(mid, "mall: client %s: RPC_RECONFIGURE2 returned %d", c.clid, rpcret);
  }
}

/* Message stream */
void
mstream_th(void *arg)
{
  struct mstream *data = (struct mstream *)arg;

  if (!data->icdbs) {
    LOG_ERROR(data->mid, "null ICDB context");
    return;
  }

  int ret, xrank;
  ret = ABT_self_get_xstream_rank(&xrank);
  if (ret != ABT_SUCCESS) {
    LOG_ERROR(data->mid, "argobots ES rank");
    return;
  }

  struct icdb_context *icdb = data->icdbs[xrank];
  struct icdb_beegfs status;
  margo_debug(data->mid, "message thread: listening on beegfs stream");
  do {
    /* blocking read from FS stream */
    ret = icdb_mstream_beegfs(icdb, &status);
    if (ret != ICDB_SUCCESS) { return; }
    if (status.timestamp != 0) {
      margo_debug(data->mid, "beegfs:qlen:%"PRIu64" %"PRIu32, status.timestamp, status.qlen);
    }
    if (status.qlen > 10) {
      mall_shrink(data->mid, data->rpcids, icdb);
    }
  } while (ret == ICDB_SUCCESS);
  return;
}
