/**
 * Synthetic MPI application with distinct IO and "computation"
 * phases.
 *
 * The compute phase linearly decreases in time with the
 * number of processes.
 *
 * The IO phase accesses the file non-contiguously, with each MPI
 * process writing and reading blocks of NELEMS in a round-robin
 * fashion to achieve a total of NBYTES. The reading is shifted one
 * block, meaning each process reads the blocks its neighbour
 * wrote. This is done to avoid measuring the effect of the page
 * cache.
 *
 * TODO:
 * - remove file at the end of run
 * - cleanup error messages + return codes, add perror style abort
 */

#include <errno.h>
#include <getopt.h>             /* getopt_long */
#include <inttypes.h>           /* PRIuXX */
#include <limits.h>             /* INT_MAX */
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* EXIT_SUCCESS, (s)rand */
#include <string.h>             /* strerror */
#include <time.h>               /* clock_gettime */
#include <unistd.h>             /* sleep, getentropy */
#include <mpi.h>
#include <icc.h>

#define PRINTFROOT(rank,...) if (rank == 0 && isparent()) {     \
    printf(__VA_ARGS__);                                        \
}

#define NITER  12
#define NBYTES_DEFAULT (4ULL * 1024 * 1024 * 1024)
#define NELEMS         (1024 * 1024) /* nelems per block (int for MPI) */
#define SERIAL_SEC_DEFAULT   2U       /* duration of the serial part of the computation */
#define PARALLEL_SEC_DEFAULT 32U      /* duration of the parallel part of the computation */

#define TERMINATE_TAG   0x7
#define HOSTNAME_MAXLEN 256


static void usage(char *name);
static void errabort(int errcode);
static void expand(uint32_t nprocs, const char *hostlist, int argc, char *argv[],
                   int iteration, MPI_Comm *intercomm);

static int isparent(void);
static void terminate_parent(MPI_Comm *intercomm, struct icc_context *icc);
static void terminate_child(MPI_Comm *intercomm);
static int fileopen(const char *filepath, int nblocks, int nelems, int nprocs,
                    MPI_File *fh, MPI_Datatype *filetype);
static int nblocks_per_procs(unsigned long long nbytes, int nelems, int nprocs,
                             int *nblocks);
static int timediff_ms(struct timespec start, struct timespec end, unsigned long *res_ms);

static void compute(unsigned int serial_sec, unsigned int parallel_sec,
                    int isroot, MPI_Comm intracomm, MPI_Comm intercomm);
static int io(int nblocks, int nelems, int rank, int nprocs,
              MPI_Datatype filetype, MPI_File fh);

int
main(int argc, char **argv)
{
  unsigned long long nbytes = NBYTES_DEFAULT;
  unsigned int serial_sec = SERIAL_SEC_DEFAULT;
  unsigned int parallel_sec = PARALLEL_SEC_DEFAULT;

  static struct option longopts[] = {
    { "size", required_argument, NULL, 's' },
    { "serial-time", required_argument, NULL, 'l' },
    { "parallel-time", required_argument, NULL, 'p' },
    { NULL,    0,                NULL,  0  },
  };

  int ch;
  char *endptr;
  unsigned long tmp;

  while ((ch = getopt_long(argc, argv, "s:l:p:", longopts, NULL)) != -1)
    switch (ch) {
    case 's':
      nbytes = strtoull(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0') {
        fputs("Invalid argument: size\n", stderr);
        exit(EXIT_FAILURE);
      }
      break;
    case 'l':
      tmp = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0' || tmp > UINT_MAX) {
        fputs("Invalid argument: serial-time\n", stderr);
        exit(EXIT_FAILURE);
      }
      serial_sec = (unsigned int)tmp;
      break;
    case 'p':
      tmp = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0' || tmp > UINT_MAX) {
        fputs("Invalid argument: parallel-time\n", stderr);
        exit(EXIT_FAILURE);
      }
      parallel_sec = (unsigned int)tmp;
      break;
    case 0:
      continue;
    default:
      usage(argv[0]);
      exit(EXIT_FAILURE);
    }

  if (argc - optind < 1) {
    fputs("Missing file path\n", stderr);
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  char *filepath = argv[optind];

  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    fputs("Multithreading not supported\n", stderr);
    MPI_Finalize();
    exit(EXIT_FAILURE);
  }

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  int rc = 0;
  int nblocks = -1;

  rc = nblocks_per_procs(nbytes, NELEMS, nprocs, &nblocks);
  if (rc < 0) {
    errabort(-rc);
  } else if (nblocks == 0) {
    fputs("Not enough bytes to write\n", stderr);
    errabort(EINVAL);
  }

  struct icc_context *icc = NULL;
  if (rank == 0 && isparent()) {
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, nprocs, NULL, NULL, &icc);
    if (!icc) {
      fputs("ICC could not be initialized\n", stderr);
      MPI_Finalize();
      return EXIT_FAILURE;
    }
  }

  MPI_File fh = MPI_FILE_NULL;
  MPI_Datatype filetype = MPI_DATATYPE_NULL;
  /* XX check return code */
  fileopen(filepath, nblocks, NELEMS, nprocs, &fh, &filetype);

  MPI_Comm intercomm = MPI_COMM_NULL;
  int iter = 0;
  int terminate = 0;

  if (!isparent()) {
    MPI_Comm_get_parent(&intercomm);
    /* get current iteration from parent */
    MPI_Bcast(&iter, 1, MPI_INT, 0, intercomm);
  }

  for (; iter < NITER; iter++) {
    if (isparent()) {
      enum icc_reconfig_type rct;
      uint32_t nprocs_spawn;
      const char *hostlist = NULL; /* must be null for all ranks except root */

      if (rank == 0) {
        rc = icc_reconfig_pending(icc, &rct, &nprocs_spawn, &hostlist);
        if (rc != ICC_SUCCESS) {
          errabort(rc);
        }
      }

      /* broadcast the order to all parent processes */
      MPI_Bcast(&rct, 1, MPI_INT, 0, MPI_COMM_WORLD);

      switch (rct) {
      case ICC_RECONFIG_EXPAND:
        terminate = 0;
        expand(nprocs_spawn, hostlist, argc, argv, iter, &intercomm);
        break;
      case ICC_RECONFIG_SHRINK:
        terminate = 1;
        break;
      default:
        terminate = 0;
        break;
      }
    }

    /* termination order + sync point for children */
    if (isparent()) {
      if (intercomm != MPI_COMM_NULL)
        MPI_Bcast(&terminate, 1, MPI_INT, rank == 0 ? MPI_ROOT : MPI_PROC_NULL,
                  intercomm);
      if (terminate) {
        terminate_parent(&intercomm, icc);
      }
    } else {
      MPI_Bcast(&terminate, 1, MPI_INT, 0, intercomm);
      if (terminate) {
        terminate_child(&intercomm);
        return 0;
      }
    }

    struct timespec start, end;
    unsigned long elapsed_io = ULONG_MAX, elapsed_compute = ULONG_MAX;

    if (isparent()) {
      rc = clock_gettime(CLOCK_MONOTONIC, &start);
      if (rc) {
        errabort(rc);
      }

      rc = io(nblocks, NELEMS, rank, nprocs, filetype, fh);
      if (rc) {
        errabort(-rc);
      }
      rc = clock_gettime(CLOCK_MONOTONIC, &end);
      if (rc) {
        errabort(rc);
      }

      if (timediff_ms(start, end, &elapsed_io)) {
        elapsed_io = 0;         /* overflow */
      }
    }

    /* sync parent and child before starting the timer */
    if (intercomm != MPI_COMM_NULL)
      MPI_Barrier(intercomm);

    rc = clock_gettime(CLOCK_MONOTONIC, &start);
    if (rc) {
      errabort(rc);
    }

    compute(serial_sec, parallel_sec,
            rank == 0 && isparent(), MPI_COMM_WORLD, intercomm);

    rc = clock_gettime(CLOCK_MONOTONIC, &end);
    if (rc) {
      errabort(rc);
    }

    if (timediff_ms(start, end, &elapsed_compute)) {
      elapsed_compute = 0;      /* overflow */
    }

    PRINTFROOT(rank, "Iteration %d: %ldms IO (%lld KB/s), %ldms compute\n", iter, elapsed_io, nbytes / elapsed_io, elapsed_compute);

  } /* end iteration */

  if (fh != MPI_FILE_NULL) {
    MPI_File_close(&fh);
  }
  if (filetype != MPI_DATATYPE_NULL) {
    MPI_Type_free(&filetype);
  }

  MPI_Finalize();
  return EXIT_SUCCESS;
}

static void
usage(char *name)
{
  fprintf(stderr, "Usage: %s [--size TOTALSIZE] FILEPATH\n", name);
}

static void
errabort(int errcode)
{
  /* XX reuse errno message */
  fprintf(stderr, "error: %s\n", strerror(errcode));
  MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

static int
isparent()
{
  MPI_Comm intercomm;
  MPI_Comm_get_parent(&intercomm);

  if (intercomm == MPI_COMM_NULL) {
    return 1;
  } else {
    return 0;
  }
}

static int
fileopen(const char *filepath, int nblocks, int nelems, int nprocs,
         MPI_File *fh, MPI_Datatype *filetype)
{
  MPI_Info info;
  MPI_Info_create(&info);

  MPI_File_set_errhandler(*fh, MPI_ERRORS_ARE_FATAL);
  MPI_File_open(MPI_COMM_WORLD, filepath,
                MPI_MODE_RDWR | MPI_MODE_CREATE,
                info, fh);

  MPI_Info_free(&info);

  int stride;
  if (__builtin_smul_overflow(nelems, nprocs, &stride)) {
    return -EOVERFLOW;
  }
  MPI_Type_vector(nblocks, nelems, stride, MPI_INT, filetype);
  MPI_Type_commit(filetype);

  return 0;
}

static int
nblocks_per_procs(unsigned long long nbytes, int nelems, int nprocs, int *nblocks)
{
  unsigned int nbytes_per_block;
  unsigned int nbytes_per_segment;       /* a segment is a block per procs */

  if (__builtin_mul_overflow(nelems, sizeof(int), &nbytes_per_block)) {
    return -EOVERFLOW;
  }

  if (__builtin_mul_overflow(nbytes_per_block, nprocs, &nbytes_per_segment)) {
    return -EOVERFLOW;
  }

  unsigned long long nblocks_per_proc = nbytes / nbytes_per_segment;

  /* we want an int for MPI, so detect possible overflow */
  if (nblocks_per_proc <= INT_MAX) {
    *nblocks = (int)nblocks_per_proc;
  } else {
    return -EOVERFLOW;
  }

  return 0;
}

/**
 * Set RES_MS to the number of ms elapsed between START and
 * END. Return 0 or EOVERFLOW on overflow.
 */
static int
timediff_ms(struct timespec start, struct timespec end, unsigned long *res_ms)
{
  struct timespec diff;

  diff.tv_sec = end.tv_sec - start.tv_sec;
  diff.tv_nsec = end.tv_nsec - start.tv_nsec;
  if (diff.tv_nsec < 0) {
    diff.tv_sec--;
    diff.tv_nsec += 1000000000L;
  }

  /* negative time makes no sense for a time difference. After this
     check tv_sec & tv_nsec can be cast to unsigned types */
  if (diff.tv_sec < 0 || diff.tv_nsec < 0) {
    return EINVAL;
  }

  unsigned long ms;
  if (__builtin_mul_overflow((uintmax_t)diff.tv_sec, 1000U, &ms)) {
    return EOVERFLOW;
  }

  /* XX floating point division? */
  if (__builtin_uaddl_overflow(ms, (unsigned long)diff.tv_nsec / 1000000UL, res_ms)) {
    return EOVERFLOW;
  }

  return 0;
}

static void
compute(unsigned int serial_sec, unsigned int parallel_sec,
        int isroot, MPI_Comm intracomm, MPI_Comm intercomm)
{
  if (isroot) {
    unsigned int total_sleep;
    int n,  nprocs = 0, nchilds = 0;
    MPI_Comm_size(intracomm, &nprocs);
    if (intercomm != MPI_COMM_NULL) {
      MPI_Comm_remote_size(intercomm, &nchilds);
    }

    n = nprocs + nchilds;

    if (n < 0) {  /* should not happen , but MPI returns ints... */
      n = 1;
    }

    total_sleep = serial_sec + parallel_sec / (unsigned int)n;
    if (total_sleep < serial_sec) { /* overflow */
      total_sleep = UINT_MAX;
    }

    sleep(total_sleep);
  }

  if (intercomm != MPI_COMM_NULL) {
    MPI_Barrier(intercomm);
  } else {
    MPI_Barrier(intracomm);
  }
}


static int
io(int nblocks, int nelems, int rank, int nprocs,
   MPI_Datatype filetype, MPI_File fh)
{
  if (nblocks < 0 || nelems < 0) {
    return -EINVAL;
  }

  size_t blocksize = (unsigned int)nelems * sizeof(int);
  if (blocksize < (size_t)nelems) {
    return -EOVERFLOW;
  }

  size_t bufsize = blocksize * (unsigned int)nblocks;
  if (bufsize < blocksize) {
    return -EOVERFLOW;
  }

  int ntotal;
  if (__builtin_mul_overflow(nelems, nblocks, &ntotal)) {
    return -EOVERFLOW;
  }

  int *buf = malloc(bufsize);
  if (!buf) {
    return -ENOMEM;
  }
  for (size_t i = 0; i < (size_t)ntotal; i++) {
    memcpy(buf + i, &rank, sizeof(rank));
  }

  MPI_Offset offset;
  if (__builtin_mul_overflow(blocksize, rank, &offset)) {
    free(buf);
    return -EOVERFLOW;
  }

  MPI_File_set_view(fh, offset, MPI_INT, filetype, "native", MPI_INFO_NULL);
  MPI_File_write_all(fh, buf, ntotal, MPI_INT, MPI_STATUS_IGNORE);

  /* read blocks from the next process */
  int next_rank;
  if (__builtin_sadd_overflow(rank, 1, &next_rank)) {
    free(buf);
    return -EOVERFLOW;
  }
  next_rank = next_rank % nprocs;

  if (__builtin_mul_overflow(blocksize, next_rank, &offset)) {
    free(buf);
    return -EOVERFLOW;
  }

  MPI_File_set_view(fh, offset, MPI_INT, filetype, "native", MPI_INFO_NULL);
  MPI_File_read_all(fh, buf, ntotal, MPI_INT, MPI_STATUS_IGNORE);

  /* check result */
  for (size_t i = 0; i < (size_t)ntotal; i++) {
    if (*(buf + i) != next_rank) {
      fprintf(stderr,"INT %zd: %d instead of %d\n", i, *(buf + i), next_rank);
      free(buf);
      return -EINVAL;
    }
  }

  free(buf);
  return 0;
}

static void
expand(uint32_t nprocs, const char *hostlist, int argc, char *argv[],
       int iteration, MPI_Comm *intercomm)
{
  /* rank in parent processes*/
  int rank = -1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  MPI_Info hostinfo = MPI_INFO_NULL;
  if (hostlist) {
    MPI_Info_create(&hostinfo);
    MPI_Info_set(hostinfo, "host", hostlist);
  }

  char **child_argv = malloc((unsigned int)argc * sizeof(*child_argv)); /* assume no overflow */

  for (int i = 0; i < argc - 1; i++) {
    child_argv[i] = argv[i + 1];
  }
  child_argv[argc - 1] = NULL;

  if (nprocs > INT_MAX) {
    nprocs = 0;
  }

  PRINTFROOT(rank, "Spawning %s on %s (%"PRIu32" procs, iter %d)\n",
             argv[0], hostlist, nprocs, iteration);

  MPI_Comm_spawn(argv[0], child_argv, (int)nprocs, hostinfo, 0,
                 MPI_COMM_WORLD, intercomm, MPI_ERRCODES_IGNORE);

  free(child_argv);

  if (hostinfo != MPI_INFO_NULL) {
    MPI_Info_free(&hostinfo);
  }

  /* send current iteration to children */
  MPI_Bcast(&iteration, 1, MPI_INT, rank == 0 ? MPI_ROOT : MPI_PROC_NULL,
            *intercomm);
}


static void
terminate_parent(MPI_Comm *intercomm, struct icc_context *icc)
{
  if (*intercomm == MPI_COMM_NULL) { /* nothing to shrink */
    return;
  }

  int nchilds;
  MPI_Comm_remote_size(*intercomm, &nchilds);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank != 0) {
    MPI_Comm_disconnect(intercomm);
  } else {
    for (int i = 0; i < nchilds; i++ ) {
      char hostname[HOSTNAME_MAXLEN];
      MPI_Recv(hostname, HOSTNAME_MAXLEN, MPI_CHAR, i, TERMINATE_TAG, *intercomm,
               MPI_STATUS_IGNORE);

      size_t hostlen = strnlen(hostname, HOSTNAME_MAXLEN);
      if (hostlen == HOSTNAME_MAXLEN) {  /* string not terminated */
        exit(EXIT_FAILURE);
      } else {
        icc_release_register(icc, hostname, 1);
      }
    }

    MPI_Comm_disconnect(intercomm);
    /* warning: nodes must be released AFTER disconnection from the
       intercommunicator, otherwise PMI proxies are still running */

    /* give some time to PMI proxies to exit (XX hackish) */
    sleep(2);
    icc_release_nodes(icc);
  }
}

static void
terminate_child(MPI_Comm *intercomm)
{
  char *procname = getenv("SLURMD_NODENAME");
  if (!procname) {
    errabort(EINVAL);
  }

  size_t len = strlen(procname);
  if (len > INT_MAX) {
    len = INT_MAX;
  }

  MPI_Send(procname, (int)len, MPI_CHAR, 0, TERMINATE_TAG, *intercomm);
  MPI_Comm_disconnect(intercomm);

  MPI_Finalize();
}
