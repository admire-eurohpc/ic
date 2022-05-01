/**
 * Example MPI application using MPI_Comm_spawn.
 *
 * TODO:
 * XX Multithreading needs to be addressed
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>             /* getenv */
#include <string.h>             /* str(n)len */
#include <unistd.h>             /* sleep */
#include <mpi.h>
#include <icc.h>


#define TERMINATE_TAG   0x7
#define ITER_MAX        12
#define HOSTNAME_MAXLEN 256

struct reconfig_data{
  MPI_Comm           intercomm;
  char               *command;
  struct icc_context *icc;
};


int reconfigure(int shrink, uint32_t maxprocs, const char *hostlist, void *data);


int
main(int argc, char **argv)
{
  int rank, provided, flag, size, *usize;
  char ischild;
  struct reconfig_data data;

  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    fputs("Multithreading not supported\n",stderr);
    MPI_Finalize();
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  data.command = argv[0];
  data.intercomm = MPI_COMM_NULL;

  MPI_Comm_get_parent(&(data.intercomm));
  ischild = data.intercomm != MPI_COMM_NULL ? 1 : 0;

  /* root only */
  if (!ischild && rank == 0) {
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, size, reconfigure, &data, &data.icc);
    assert(data.icc);
  }

  /* "computation" loop */
  unsigned int iter = 0;
  while (iter < ITER_MAX) {
    /* MPI_Get_processor_name != "Slurmd nodename" in emulation env */
    char *procname = getenv("SLURMD_NODENAME");

    /* get the "universe size" = number of possible useful process
       (optional per the standard, set only at app start time) */
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &usize, &flag);
    if (flag) {
      printf("%s: Running on %s, rank %d (universe %u)\n",
             ischild ? "child" : "parent ", procname ? procname : "NONODE", rank, *usize);
    } else if (ischild) {
      printf("child: Running on %s, rank %d\n",
              procname ? procname : "NONODE", rank);
    } else {
      printf("parent: Running on %s, rank %d (iteration %u)\n",
             procname ? procname : "NONODE", rank, iter);

    }

    if (ischild) {
      /* check for incoming terminate message */
      int terminate;
      MPI_Iprobe(0, TERMINATE_TAG, data.intercomm, &terminate, MPI_STATUS_IGNORE);

      if (terminate) {
        /* ack termination and send our nodename */
        MPI_Recv(NULL, 0, MPI_INT, 0, TERMINATE_TAG, data.intercomm,
                 MPI_STATUS_IGNORE);
        MPI_Send(procname, strlen(procname), MPI_CHAR, 0, TERMINATE_TAG,
                  data.intercomm);
        MPI_Finalize();
      }
    }

    ++iter;
    sleep(2);
  }

  if (!ischild && rank == 0) {
    MPI_Finalize();

    sleep(2);                   /* give some time to PMI proxies to exit */

    icc_release_nodes(data.icc);
    icc_fini(data.icc);
  }
}


int
reconfigure(int shrink, uint32_t maxprocs, const char *hostlist, void *data)
{
  MPI_Info hostinfo;
  struct reconfig_data *d = (struct reconfig_data *)data;

  if (!shrink) {
    fprintf(stderr, "IN RECONFIG: %d procs on %s\n", maxprocs, hostlist);

    MPI_Info_create(&hostinfo);
    MPI_Info_set(hostinfo, "host", hostlist);

    MPI_Comm_spawn(d->command, MPI_ARGV_NULL, maxprocs, hostinfo, 0,
                 MPI_COMM_SELF, &(d->intercomm), MPI_ERRCODES_IGNORE);

    MPI_Info_free(&hostinfo);
  } else {
    fprintf(stderr, "IN RECONFIG: shrink %d procs\n", maxprocs);

    MPI_Request *reqs = malloc(maxprocs * sizeof(*reqs));

    for (uint32_t i = 0; i < maxprocs; i++ ) {
      MPI_Isend(NULL, 0, MPI_INT, i, TERMINATE_TAG, d->intercomm,
                &reqs[i]);
    }
    MPI_Waitall(maxprocs, reqs, MPI_STATUSES_IGNORE);

    free(reqs);

    for (uint32_t i = 0; i < maxprocs; i++ ) {
      char hostname[HOSTNAME_MAXLEN];
      MPI_Recv(hostname, HOSTNAME_MAXLEN, MPI_CHAR, i, TERMINATE_TAG, d->intercomm,
               MPI_STATUS_IGNORE);

      size_t hostlen = strnlen(hostname, HOSTNAME_MAXLEN);
      if (hostlen == HOSTNAME_MAXLEN) {  /* string not terminated */
        exit(EXIT_FAILURE);
      } else {                           /*  release processor */
        fprintf(stderr, "Registering 1 processor for release on %s\n", hostname);
        icc_release_register(d->icc, hostname, 1);
      }
    }
  }

  return 0;
}
