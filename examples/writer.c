/**
 * Write a configurable amount of bytes to a configurable file,
 * repeatedly, for a configurable amount of phases. Sleep between
 * phases to reach the characteristic time specified in witer.
 **/

#include <errno.h>
#include <getopt.h>             /* getopt_long */
#include <limits.h>             /* INT_MAX */
#include <stdio.h>              /* (f)printf */
#include <stdlib.h>             /* strtol */
#include <string.h>             /* strerror */
#include <time.h>               /* nanosleep */

#include <mpi.h>


#define NTOTAL_DEFAULT INT_MAX  /* (2Gi -1) */
#define NPHASES_DEFAULT 1UL
#define TIMESPEC_DIFF(end, start,r)                     \
  (r).tv_sec = (end).tv_sec - (start).tv_sec;           \
  (r).tv_nsec = (end).tv_nsec - (start).tv_nsec;        \
  if ((r).tv_nsec < 0) {                                \
    (r).tv_sec--;                                       \
    (r).tv_nsec += 1000000000L;                         \
  }


static void
usage(char *name)
{
  fprintf(stderr, "Usage: %s --witer [--size --phases] FILEPATH\n", name);
}


int main(int argc, char *argv[])
{

  long int witer_s = 0;
  struct timespec witer = { 0 };

  unsigned long ntotal = NTOTAL_DEFAULT;
  unsigned long nphases = NPHASES_DEFAULT;

  static struct option longopts[] = {
    { "witer",  required_argument, NULL, 'w' },
    { "size",   required_argument, NULL, 's' },
    { "phases", required_argument, NULL, 'n' },
    { NULL,     0,                 NULL,  0  },
  };

  int ch;
  char *endptr;

  while ((ch = getopt_long(argc, argv, "w:s:n:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'w':
      witer_s = strtol(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: witer\n", stderr);
        exit(EXIT_FAILURE);
      }
      witer.tv_sec = witer_s;   /* assumes time_t is at least a long */
      break;
    case 's':
      ntotal = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: size\n", stderr);
        exit(EXIT_FAILURE);
      }
      break;
    case 'n':
      nphases = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: phases\n", stderr);
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

  if (witer.tv_sec <= 0) {
    fputs("Invalid argument: witer\n", stderr);
    exit(EXIT_FAILURE);
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
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  MPI_File_set_errhandler(fh, MPI_ERRORS_ARE_FATAL);

  unsigned long nbytes = ntotal / (unsigned)nprocs;

  /* MPI takes an int number of elements, make sure we can cast safely */
  if (nbytes > INT_MAX) {
    fprintf(stderr, "Too many bytes to write (%lu)\n", nbytes);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  int *buf = malloc(nbytes);
  if (!buf) {
    fputs("Out of memory\n", stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  struct timespec start, end, res;
  for (unsigned int i = 0; i < nphases; i++) {
    /* write the content of the buffer directly, we do not care about
       the content */

    rc = clock_gettime(CLOCK_MONOTONIC, &start);
    MPI_File_write_shared(fh, buf, (int)nbytes, MPI_BYTE, MPI_STATUS_IGNORE);
    rc = clock_gettime(CLOCK_MONOTONIC, &end);

    if (rank == 0) {
      TIMESPEC_DIFF(end, start, res);
      TIMESPEC_DIFF(witer, res, res);
      if (res.tv_sec <= 0) {
        fprintf(stderr, "Warning: witer is too small (%lds)\n", witer.tv_sec);
      } else {
        if (nanosleep(&res, NULL)) {
          fprintf(stderr, "Error in sleep: %s\n", strerror(errno));
          MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }

  free(buf);
  MPI_File_close(&fh);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
