/**
 * Synthetic MPI application with distinct IO and "computation"
 * phases.
 */

#include <errno.h>
#include <limits.h>             /* INT_MAX */
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* EXIT_SUCCESS, (s)rand */
#include <string.h>             /* strerror */
#include <unistd.h>             /* sleep, getentropy */
#include <mpi.h>

#define NBLOCKS 8
#define NELEMS 8          /* nelems in block */

static void usage(int rank, char *name);
static void errabort(int errcode);
static int filetype_set(int nblocks, int nelems, int nprocs,
                        MPI_Datatype *filetype);

static void compute(int rank, int nprocs, MPI_Comm comm);
static int io(int nblocks, int nelems, int rank, int nprocs,
              MPI_Datatype filetype, MPI_File fh);


int
main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  int rc = 0;

  if (argc < 2) {
    usage(rank, argv[0]);
    MPI_Finalize();
    return EXIT_FAILURE;
  }

  MPI_File fh = MPI_FILE_NULL;
  MPI_Datatype filetype = MPI_DATATYPE_NULL;

  MPI_File_set_errhandler(fh, MPI_ERRORS_ARE_FATAL);

  /* XX create file on open/delete on close */
  MPI_File_open(MPI_COMM_WORLD, argv[1], MPI_MODE_RDWR | MPI_MODE_CREATE,
                MPI_INFO_NULL, &fh);

  rc = filetype_set(NBLOCKS, NELEMS, nprocs, &filetype);
  if (rc) {
    errabort(-rc);
  }

  rc = io(NBLOCKS, NELEMS, rank, nprocs, filetype, fh);
  if (rc) {
    errabort(-rc);
  }

  compute(rank, nprocs, MPI_COMM_WORLD);

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
usage(int rank, char *name)
{
  if (rank == 0) {
    fprintf(stderr, "usage: %s <filepath>\n", name);
  }
}

static void
errabort(int errcode)
{
  /* XX reuse errno message */
  fprintf(stderr, "error: %s\n", strerror(errcode));
  MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

static int
filetype_set(int nblocks, int nelems, int nprocs, MPI_Datatype *filetype)
{
  int stride;
  if (__builtin_smul_overflow(nelems, nprocs, &stride)) {
    return -EOVERFLOW;
  }
  MPI_Type_vector(nblocks, nelems, stride, MPI_INT, filetype);
  MPI_Type_commit(filetype);
  return 0;
}


#define SERIAL_SEC   1
#define PARALLEL_SEC 2

static void
compute(int rank, int nprocs, MPI_Comm comm)
{
  if (rank == 0) {
    sleep(SERIAL_SEC + PARALLEL_SEC / nprocs);
  }
  MPI_Barrier(comm);
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
    return -EOVERFLOW;
  }

  MPI_File_set_view(fh, offset, MPI_INT, filetype, "native", MPI_INFO_NULL);
  MPI_File_write_all(fh, buf, ntotal, MPI_INT, MPI_STATUS_IGNORE);

  /* read blocks from the next process */
  int next_rank;
  if (__builtin_sadd_overflow(rank, 1, &next_rank)) {
    return -EOVERFLOW;
  }
  next_rank = next_rank % nprocs;

  if (__builtin_mul_overflow(blocksize, next_rank, &offset)) {
    return -EOVERFLOW;
  }

  MPI_File_set_view(fh, offset, MPI_INT, filetype, "native", MPI_INFO_NULL);
  MPI_File_read_all(fh, buf, ntotal, MPI_INT, MPI_STATUS_IGNORE);

  /* check result */
  for (size_t i = 0; i < (size_t)ntotal; i++) {
    if (*(buf + i) != next_rank) {
      fprintf(stderr,"INT %zd: %d instead of %d\n", i, *(buf + i), next_rank);
      return -EINVAL;
    }
  }

  return 0;
}
