/**
 * Write a configurable amount of bytes to a configurable file, in a
 * configurable amount of phases, and sleep between phases.
 **/

#include <errno.h>
#include <getopt.h>             /* getopt_long */
#include <limits.h>             /* INT_MAX */
#include <stdio.h>              /* (f)printf */
#include <stdlib.h>             /* strtol */

#include <mpi.h>

#define NTOTAL_DEFAULT (2UL * 1024 * 1024 * 1024)

static void
usage(char *name)
{
  fprintf(stderr, "Usage: %s [--size TOTALSIZE] FILEPATH\n", name);
}


int main(int argc, char *argv[])
{

  unsigned long ntotal = NTOTAL_DEFAULT;

  static struct option longopts[] = {
    { "size", required_argument, NULL, 's' },
    { NULL,    0,                NULL,  0  },
  };

  int ch;
  char *endptr;

  while ((ch = getopt_long(argc, argv, "s:", longopts, NULL)) != -1) {
    switch (ch) {
    case 's':
      ntotal = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: size\n", stderr);
        exit(EXIT_FAILURE);
      }
      break;
    case 0:
      continue;
    default:
      usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (argc - optind < 1) {
    fputs("Missing file path\n", stderr);
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }
  char *filepath = argv[optind];

  MPI_Init(&argc, &argv);

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  MPI_File fh;
  int rc;
  rc = MPI_File_open(MPI_COMM_WORLD, filepath, MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);
  if (rc != MPI_SUCCESS) {
    fprintf(stderr, "Could not open file \"%s\"\n", filepath);
    exit(EXIT_FAILURE);
  }

  MPI_File_set_errhandler(fh, MPI_ERRORS_ARE_FATAL);

  unsigned long nbytes = ntotal / (unsigned)nprocs;
  if (nbytes > INT_MAX) {
    fputs("Too many bytes to write\n", stderr);
    exit(EXIT_FAILURE);
  }

  int *buf = malloc(nbytes);
  if (!buf) {
    fputs("Out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }

  MPI_File_write_shared(fh, buf, (int)nbytes, MPI_BYTE, MPI_STATUS_IGNORE);

  free(buf);
  MPI_File_close(&fh);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
