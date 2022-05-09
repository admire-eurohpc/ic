/**
 * Synthetic MPI application with distinct IO and "computation"
 * phases.
 */

#include <stdlib.h>             /* EXIT_SUCCESS */
#include <unistd.h>             /* sleep */
#include <mpi.h>

#define SERIAL_SEC   4
#define PARALLEL_SEC 8

int
main(int argc, char **argv)
{

  MPI_Init(&argc, &argv);

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  if (rank == 0) {
    sleep(SERIAL_SEC + PARALLEL_SEC / nprocs);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return EXIT_SUCCESS;
}

