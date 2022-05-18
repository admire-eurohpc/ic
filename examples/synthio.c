/**
 * Synthetic MPI application with distinct IO and "computation"
 * phases.
 */

#include <errno.h>
#include <inttypes.h>           /* PRIuXX */
#include <limits.h>             /* INT_MAX */
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* EXIT_SUCCESS, (s)rand */
#include <string.h>             /* strerror */
#include <time.h>               /* difftime */
#include <unistd.h>             /* sleep, getentropy */
#include <sys/time.h>           /* gettimeofday */
#include <mpi.h>
#include <icc.h>

#define PRINTFROOT(rank,...) if (rank == 0) {                   \
    printf("%s: ", isparent() ? "parent" : "child"); \
    printf(__VA_ARGS__);                                        \
}

#define NBLOCKS 128
#define NELEMS 65536            /* nelems in block */
#define NITER  20

#define TERMINATE_TAG   0x7
#define HOSTNAME_MAXLEN 256


static void usage(char *name);
static void errabort(int errcode);
static void expand(uint32_t nprocs, const char *hostlist, const char *executable,
                   char *filepath, int iteration, MPI_Comm *intercomm);
static void shrink(MPI_Comm *intercomm, struct icc_context *icc);

static int isparent(void);
static int childterminate();
static int fileopen(const char *filepath, int nblocks, int nelems, int nprocs,
                    MPI_File *fh, MPI_Datatype *filetype);

static void compute(int isroot, MPI_Comm intracomm, MPI_Comm intercomm);
static int io(int nblocks, int nelems, int rank, int nprocs,
              MPI_Datatype filetype, MPI_File fh);

int
main(int argc, char **argv)
{
  if (argc < 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    fputs("Multithreading not supported\n", stderr);
    MPI_Finalize();
  }

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  struct icc_context *icc;
  if (rank == 0 && isparent()) {
    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_MPI, nprocs, NULL, NULL, &icc);
  }

  int rc = 0;

  MPI_File fh = MPI_FILE_NULL;
  MPI_Datatype filetype = MPI_DATATYPE_NULL;
  fileopen(argv[1], NBLOCKS, NELEMS, nprocs, &fh, &filetype);

  MPI_Comm intercomm = MPI_COMM_NULL;
  int iter = 0;

  if (!isparent()) {
    MPI_Comm_get_parent(&intercomm);
    MPI_Bcast(&iter, 1, MPI_INT, 0, intercomm);
  }

  for (; iter < NITER; iter++) {
    PRINTFROOT(rank, "Iteration %d\n", iter);

    if (isparent()) {
      if (rank == 0) {
        enum icc_reconfig_type rct;
        uint32_t nprocs;
        const char *hostlist;
        rc = icc_reconfig_pending(icc, &rct, &nprocs, &hostlist);
        if (rc != ICC_SUCCESS) {
          errabort(rc);
        }

        if (rct == ICC_RECONFIG_EXPAND) {
          puts("EXPANDING");
          expand(nprocs, hostlist, argv[0], argv[1], iter, &intercomm);
        } else if (rct == ICC_RECONFIG_SHRINK) {
          puts("SHRINKING");
          shrink(&intercomm, icc);
        } else {
          if (intercomm != MPI_COMM_NULL) MPI_Barrier(intercomm);
        }
      } else {
          if (intercomm != MPI_COMM_NULL) MPI_Barrier(intercomm);
      }
    }

    if (childterminate()) {
      MPI_Finalize();
      return 0;
    }

    time_t start, end;

    if (isparent()) {
      start = time(NULL);

      rc = io(NBLOCKS, NELEMS, rank, nprocs, filetype, fh);
      if (rc) {
        errabort(-rc);
      }

      end = time(NULL);

      unsigned long long nbytes = NBLOCKS * NELEMS * sizeof(int) * nprocs;
      double elapsed = difftime(end, start);
      PRINTFROOT(rank, "IO: %.0fs (%.2e B/s)\n", elapsed, nbytes / elapsed);
    }

    start = time(NULL);

    compute(rank == 0 && isparent(), MPI_COMM_WORLD, intercomm);

    end = time(NULL);
    PRINTFROOT(rank, "Compute: %.0fs\n", difftime(end, start));

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
  fprintf(stderr, "usage: %s <filepath>\n", name);
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
childterminate(void)
{
  MPI_Comm parent;
  MPI_Comm_get_parent(&parent);

  /* parent do not terminate */
  if (parent == MPI_COMM_NULL) {
    return 0;
  }

  MPI_Barrier(parent);          /* wait for parents */

  int terminate;
  MPI_Iprobe(0, TERMINATE_TAG, parent, &terminate, MPI_STATUS_IGNORE);

  if (terminate) {
    /* ack termination and send our nodename */
    MPI_Recv(NULL, 0, MPI_INT, 0, TERMINATE_TAG, parent, MPI_STATUS_IGNORE);

    char *procname = getenv("SLURMD_NODENAME");
    if (!procname) {
      errabort(EINVAL);
    }

    MPI_Send(procname, strlen(procname), MPI_CHAR, 0, TERMINATE_TAG, parent);
    MPI_Comm_disconnect(&parent);
  }

  return terminate;
}

static int
fileopen(const char *filepath, int nblocks, int nelems, int nprocs,
         MPI_File *fh, MPI_Datatype *filetype)
{
  MPI_File_set_errhandler(*fh, MPI_ERRORS_ARE_FATAL);
  MPI_File_open(MPI_COMM_WORLD, filepath,
                MPI_MODE_RDWR | MPI_MODE_CREATE,
                MPI_INFO_NULL, fh);

  int stride;
  if (__builtin_smul_overflow(nelems, nprocs, &stride)) {
    return -EOVERFLOW;
  }
  MPI_Type_vector(nblocks, nelems, stride, MPI_INT, filetype);
  MPI_Type_commit(filetype);

  return 0;
}


#define SERIAL_SEC   1
#define PARALLEL_SEC 4

static void
compute(int isroot, MPI_Comm intracomm, MPI_Comm intercomm)
{
  if (isroot) {
    int n, nprocs = 0, nchilds = 0;
    MPI_Comm_size(intracomm, &nprocs);
    if (intercomm != MPI_COMM_NULL) {
      MPI_Comm_remote_size(intercomm, &nchilds);
    }

    n = nprocs + nchilds;
    sleep(SERIAL_SEC + PARALLEL_SEC / n);
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
  size_t blocksize = nelems * sizeof(int);
  if (blocksize < (size_t)nelems) {
    return -EOVERFLOW;
  }

  size_t bufsize = blocksize * nblocks;
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
expand(uint32_t nprocs, const char *hostlist, const char *executable,
       char *filepath, int iteration, MPI_Comm *intercomm)
{
  MPI_Info hostinfo;
  MPI_Info_create(&hostinfo);
  MPI_Info_set(hostinfo, "host", hostlist);

  char *argv[2];
  argv[0] = filepath;
  argv[1] = NULL;

  fprintf(stderr, "Spawning %s %s, on %s (%"PRIu32" procs, iter %d)\n",
          executable, filepath, hostlist, nprocs, iteration);
  MPI_Comm_spawn(executable, argv, nprocs, hostinfo, 0,
                 MPI_COMM_SELF, intercomm, MPI_ERRCODES_IGNORE);

  MPI_Info_free(&hostinfo);

  MPI_Bcast(&iteration, 1, MPI_INT, MPI_ROOT, *intercomm);

  MPI_Barrier(*intercomm);
}


static void
shrink(MPI_Comm *intercomm, struct icc_context *icc)
{
  if (*intercomm == MPI_COMM_NULL) { /* nothing to shrink */
    return;
  }

  int nchilds;
  MPI_Comm_remote_size(*intercomm, &nchilds);

  MPI_Request *reqs = malloc(nchilds * sizeof(*reqs));

  for (int i = 0; i < nchilds; i++) {
    MPI_Isend(NULL, 0, MPI_INT, i, TERMINATE_TAG, *intercomm, &reqs[i]);
  }

  MPI_Barrier(*intercomm);

  MPI_Waitall(nchilds, reqs, MPI_STATUSES_IGNORE);

  free(reqs);

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
  sleep(2);                           /* give some time to PMI proxies to exit */
  icc_release_nodes(icc);
}
