/**
 * Example MPI application using MPI_Comm_spawn.
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


int reconfigure(int maxprocs, const char *hostlist, void *data);


int
main(int argc, char **argv)
{
  int rank, flag, size, usize;
  char *procname;
  char isparent;
  struct icc_context *icc;
  struct reconfig_data data;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  data.rootrank = 0;
  data.command = argv[0];
  data.intercomm = MPI_COMM_NULL;

  MPI_Comm_get_parent(&(data.intercomm));
  isparent = data.intercomm == MPI_COMM_NULL ? 1 : 0;

  if (rank == 0 && isparent) {
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, size, reconfigure, &data, &icc);
    assert(icc);
  }

  /* MPI_Get_processor_name != "Slurmd nodename" in emulation env */
  procname = getenv("SLURMD_NODENAME");
  MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE, &usize, &flag);

  printf("%s: Running on %s, rank %d (universe %u)\n",
         isparent ? "Parent" : "Child ", procname ? procname : "NONODE", rank, usize);

  if (rank == 0 && isparent) {
    sleep(2);
    icc_fini(icc);
  }

  MPI_Finalize();
}


int
reconfigure(int maxprocs, const char *hostlist, void *data)
{
  MPI_Info hostinfo;

  fprintf(stderr, "IN RECONFIG: %d procs on %s\n", maxprocs, hostlist);

  MPI_Info_create(&hostinfo);
  MPI_Info_set(hostinfo, "host", hostlist);

  struct reconfig_data *d = (struct reconfig_data *)data;

  MPI_Comm_spawn(d->command, MPI_ARGV_NULL, maxprocs, hostinfo, d->rootrank,
                 MPI_COMM_SELF, &(d->intercomm), MPI_ERRCODES_IGNORE);

  MPI_Info_free(&hostinfo);

  return 0;
}
