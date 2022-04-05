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


struct reconfig_data{
  int      rootrank;
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

  data.rootrank = 0;
  data.command = argv[0];
  data.intercomm = MPI_COMM_NULL;

  MPI_Comm_get_parent(&(data.intercomm));
  ischild = data.intercomm != MPI_COMM_NULL ? 1 : 0;

  /* root only */
  if (rank == data.rootrank && !ischild) {
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, size, reconfigure, &data, &icc);
    assert(icc);
  }

  while (sleep(2) == 0) {
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

    /* terminate the spawned processes */
    if (data.intercomm != MPI_COMM_NULL) {
      int terminate = 0;
      if (ischild) {
        fprintf(stderr, "CHILD BLOCKING ON BCAST (rank %d)\n", rank);
        MPI_Bcast(&terminate, 1, MPI_INT, data.rootrank, data.intercomm);
        fprintf(stderr, "CHILD GOT BCAST (rank %d)\n", rank);
        if (terminate)
          break;
      } else if (rank != data.rootrank) {
        MPI_Bcast(&terminate, 1, MPI_INT, MPI_PROC_NULL, data.intercomm);
      }
    }
  }

  if (rank == data.rootrank && !ischild) {
    icc_fini(icc);
  }

  fprintf(stderr, "FINALIZING (rank %d)\n", rank);
  MPI_Finalize();
}


int
reconfigure(int shrink, uint32_t maxprocs, const char *hostlist, void *data)
{
  MPI_Info hostinfo;
  struct reconfig_data *d = (struct reconfig_data *)data;

  if (shrink) {
    fprintf(stderr, "IN RECONFIG: shrink %d procs\n", maxprocs);

    int terminate = 1;
    /* only the root rank calls this function */
    MPI_Bcast(&terminate, 1, MPI_INT, MPI_ROOT, d->intercomm);
  } else {
    fprintf(stderr, "IN RECONFIG: %d procs on %s\n", maxprocs, hostlist);

    MPI_Info_create(&hostinfo);
    MPI_Info_set(hostinfo, "host", hostlist);

    MPI_Comm_spawn(d->command, MPI_ARGV_NULL, maxprocs, hostinfo, d->rootrank,
                 MPI_COMM_SELF, &(d->intercomm), MPI_ERRCODES_IGNORE);

    MPI_Info_free(&hostinfo);
  }

  return 0;
}
