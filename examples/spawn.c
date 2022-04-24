/**
 * Example MPI application using MPI_Comm_spawn.
 *
 * TODO:
 * XX Multithreading needs to be addressed
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>             /* getenv */
#include <unistd.h>             /* sleep */
#include <mpi.h>
#include <icc.h>


#define TERMINATE_TAG 0x7
#define ITER_MAX      10

struct reconfig_data{
  MPI_Comm intercomm;
  char     *command;
};


int reconfigure(int shrink, uint32_t maxprocs, const char *hostlist, void *data);


int
main(int argc, char **argv)
{
  int rank, provided, flag, size, *usize;
  char *procname;
  char ischild;
  struct icc_context *icc;
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
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, size, reconfigure, &data, &icc);
    assert(icc);
  }

  /* "computation" loop */
  unsigned int iter = 0;
  while (iter < ITER_MAX) {
    /* MPI_Get_processor_name != "Slurmd nodename" in emulation env */
    procname = getenv("SLURMD_NODENAME");

    /* get the "universe size" = number of possible useful process
       (optional per the standard, set only at app start time) */
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &usize, &flag);
    if (flag) {
      printf("%s: Running on %s, rank %d (universe %u)\n",
             ischild ? "Child" : "Parent ", procname ? procname : "NONODE", rank, *usize);
    } else {
      printf("%s: Running on %s, rank %d \n",
             ischild ? "Child" : "Parent ", procname ? procname : "NONODE", rank);
    }

    if (ischild) {
      /* check for incoming terminate message */
      int terminate;
      MPI_Iprobe(0, TERMINATE_TAG, data.intercomm, &terminate, MPI_STATUS_IGNORE);

      if (terminate) {
        /* ack termination by accepting the message */
        MPI_Recv(NULL, 0, MPI_INT, 0, TERMINATE_TAG, data.intercomm,
                 MPI_STATUS_IGNORE);
        MPI_Comm_disconnect(&data.intercomm);

        return EXIT_SUCCESS;
      }
    }

    ++iter;
    sleep(2);
  }

  if (!ischild && rank == 0) {
    icc_fini(icc);
    MPI_Finalize();
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
  }

  return 0;
}
