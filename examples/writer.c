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
#include <string.h>             /* strerror, memset */
#include <time.h>               /* nanosleep */

#include <mpi.h>
#include <icc.h>


#define DURATION_DEFAULT 200    /* total duration of the application in sec */
#define IOSHARE_DEFAULT 20      /* percent of the characteristic time spent in IO */
#define NSLICES_TOTAL 4

#define TIMESPEC_DIFF(end,start,r) {                    \
    (r).tv_sec = (end).tv_sec - (start).tv_sec;         \
    (r).tv_nsec = (end).tv_nsec - (start).tv_nsec;      \
    if ((r).tv_nsec < 0) {                              \
      (r).tv_sec--;                                     \
      (r).tv_nsec += 1000000000L;                       \
    }                                                   \
  }

#define TIMESPEC_DIFF_ACCUMULATE(end,start,acc) {       \
    struct timespec tmp;                                \
    TIMESPEC_DIFF(end, start, tmp);                     \
    (acc).tv_sec += tmp.tv_sec;                         \
    (acc).tv_nsec += tmp.tv_nsec;                       \
    if (acc.tv_nsec >= 1000000000L) {                   \
      (acc).tv_sec++;                                   \
      (acc).tv_nsec -= 1000000000L;                     \
    }                                                   \
  }

#define TIMESPEC_GET(t)                                                 \
  if (clock_gettime(CLOCK_MONOTONIC, &(t))) {                           \
    fprintf(stderr, "clock_gettime error: %s\n", strerror(errno));      \
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);                            \
  }

#define ICC_HINT_IO_BEGIN(rank,icc,witer,iter,nslices) if ((rank) == 0) { \
    if (icc_hint_io_begin((icc), (unsigned)(witer), (iter), (nslices))) { \
      fputs("icc_hint_io_begin error\n", stderr);                       \
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);                          \
    }                                                                   \
  }

#define ICC_HINT_IO_END(rank,icc,witer,islast,nbytes) if ((rank) == 0) { \
    if (icc_hint_io_end((icc), (unsigned)(witer), (islast), (nbytes))) { \
      fputs("icc_hint_io_end error\n", stderr);                         \
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);                          \
    }                                                                   \
  }

#define SAFE_LLMUL(a,b,res) if (__builtin_smulll_overflow(a, b, res)) { \
      fputs("multiplication overflows\n", stderr);                      \
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);                          \
}

static void
usage(char *name)
{
  fprintf(stderr, "Usage: %s --witer --bandwidth (MiB/s) [--duration (s) --ioshare(%%] FILEPATH\n", name);
}


int main(int argc, char *argv[])
{

  long int witer_s = 0;
  struct timespec witer = { 0 };

  long duration = DURATION_DEFAULT;
  long bandwidth = 0;
  long ioshare = IOSHARE_DEFAULT;

  static struct option longopts[] = {
    { "witer",     required_argument, NULL, 'w' },
    { "duration",  required_argument, NULL, 'h' },
    { "bandwdith", required_argument, NULL, 'b' },
    { "ioshare",   required_argument, NULL, 'i' },
    { NULL,        0,                 NULL,  0  },
  };

  int ch;
  char *endptr;

  while ((ch = getopt_long(argc, argv, "w:h:b:i:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'w':
      witer_s = strtol(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: witer\n", stderr);
        exit(EXIT_FAILURE);
      }
      witer.tv_sec = witer_s;   /* assumes time_t is at least a long */
      break;
    case 'h':
      duration = strtol(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: duration\n", stderr);
        exit(EXIT_FAILURE);
      }
      break;
    case 'b':
      bandwidth = strtol(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: bandwidth\n", stderr);
        exit(EXIT_FAILURE);
      }
      break;
    case 'i':
      ioshare = strtol(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: ioshare\n", stderr);
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

  if (bandwidth <= 0) {
    fputs("Invalid argument: bandwidth\n", stderr);
    exit(EXIT_FAILURE);
  }

  if (ioshare < 0 || ioshare > 100) {
    fputs("Invalid argument: ioshare, percent between 0 and 100\n", stderr);
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

  long long tmp;

  SAFE_LLMUL(bandwidth, witer.tv_sec, &tmp);
  SAFE_LLMUL(tmp, ioshare, &tmp);
  tmp /= 100;                         /* ioshare is % */
  SAFE_LLMUL(tmp, 1048576, &tmp);     /* to Mib */
  tmp /= nprocs;
  tmp /= sizeof(long);

  /* MPI takes an int number of elements, make sure we can cast safely */
  if (tmp < 0 || tmp > INT_MAX || (unsigned long long)tmp > SIZE_MAX) {
    fprintf(stderr, "Wrong number of bytes computed: %llu\n", tmp);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  int count = (int)tmp;
  char *buf = reallocarray(NULL, (size_t)count, sizeof(long));
  if (!buf) {
    fputs("memory allocation error\n", stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
  memset(buf, rank, (size_t)count * sizeof(long));   /* mult was checked by reallocarray already */

  /* only root rank talks to the IC */
  struct icc_context *icc;
  if (rank == 0) {
    icc_init(ICC_LOG_DEBUG, ICC_TYPE_IOSETS, &icc);
    if (!icc) {
      fputs("Could not initialize libicc\n", stderr);
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
  }

  long int niter = duration / witer.tv_sec;

  for (long int i = 0; i < niter; i++) {
    struct timespec start, end, res = { 0 };
    unsigned int nslices = 0;

    for (unsigned int j = 0; j < NSLICES_TOTAL; j++) {
      if (nslices == 0) {
        ICC_HINT_IO_BEGIN(rank, icc, witer.tv_sec, j == 0, &nslices);
        nslices = nslices > NSLICES_TOTAL ? NSLICES_TOTAL : nslices;
      }
      /* rewind file pointer + collective: wait for authorization from root rank */
      MPI_File_seek_shared(fh, 0, MPI_SEEK_SET);

      TIMESPEC_GET(start);

      MPI_File_write_shared(fh, buf, (int)count, MPI_LONG, MPI_STATUS_IGNORE);

      nslices--;

      TIMESPEC_GET(end);
      TIMESPEC_DIFF_ACCUMULATE(end, start, res);

      if (nslices == 0) { /* all slices have been written */
        int islast = 0;
        long long nbytes_total = 0;

        if (j == NSLICES_TOTAL - 1) {
          islast = 1;
          SAFE_LLMUL(NSLICES_TOTAL, count, &nbytes_total);
          SAFE_LLMUL(nbytes_total, nprocs, &nbytes_total);
        }

        ICC_HINT_IO_END(rank, icc, witer.tv_sec, islast, (unsigned long long)nbytes_total);
      }
    }

    if (rank == 0) {
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

  if (rank == 0) {
    icc_fini(icc);
  }

  free(buf);
  MPI_File_close(&fh);
  MPI_Finalize();
  return EXIT_SUCCESS;
}

