/**
 * Example MPI application for testing the connection to the IC.
 *
 * Compute the sum of an array of random integer by distributing the
 * work amongst the MPI processes.
 *
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpich/mpi.h>

#define DEFAULTSIZE 4096
#define ROOTRANK 0

int
main(int argc, char **argv)
{
    MPI_Init(NULL, NULL);

    int nprocs, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int size = DEFAULTSIZE;

    if (argc > 1) {
      char *end;
      errno = 0;
      size = strtoul(argv[1], &end, 0);
      if (errno != 0 || end == argv[1])
        exit(EXIT_FAILURE);
    }

    /* initialize random array with 0-255 value */
    int *data = malloc(size * sizeof(int));
    if (!data)
      exit(EXIT_FAILURE);

    if(rank == ROOTRANK) {
      for (int i = 0; i < size; i++)
        data[i] = rand() / (RAND_MAX / 256);
    }

    MPI_Bcast(data, size, MPI_INT, ROOTRANK, MPI_COMM_WORLD);

    int x = size / nprocs;   /* array size must be divisible by nprocs */
    int lo = rank * x;
    int hi = lo + x;
    int res = 0;
    for(int i = lo; i < hi; i++)
      res += data[i];

    printf("%d: %d\n", rank, res);

    /* compute global sum */
    int globalres = 0;
    MPI_Reduce(&res, &globalres, 1, MPI_INT, MPI_SUM, ROOTRANK, MPI_COMM_WORLD);

    if(rank == ROOTRANK) {
      printf("%d\n", globalres);
    }

    free(data);

    MPI_Finalize();
}
