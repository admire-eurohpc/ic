/**
 * Example MPI application using MPI_Comm_spawn.
 */
#include <stdio.h>
#include <mpi.h>


int
main(int argc, char **argv)
{
  int rank, len;
  char procname[MPI_MAX_PROCESSOR_NAME];
  char isparent;
  MPI_Comm intercomm;
  MPI_Info hostinfo;

  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  MPI_Comm_get_parent(&intercomm);
  isparent = intercomm == MPI_COMM_NULL ? 1 : 0;

  if (isparent) {
    MPI_Info_create(&hostinfo);
    MPI_Info_set(hostinfo, "hosts", "localhost");

    MPI_Comm_spawn(argv[0], MPI_ARGV_NULL, 2, hostinfo, 0, MPI_COMM_SELF, &intercomm, MPI_ERRCODES_IGNORE);

    MPI_Info_free(&hostinfo);
  }

  MPI_Get_processor_name(procname, &len);
  printf("%s: Running on %s, rank %d\n", isparent ? "Parent" : "Child ", procname, rank);

  MPI_Finalize();
}
