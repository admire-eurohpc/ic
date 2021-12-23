/**
 * Example MPI application for testing the connection to the IC.
 *
 * Compute the sum of an array of random integer by distributing the
 * work amongst the MPI processes.
 *
 * Malleability: dependence on SLURM_NNODES and SLURM_JOBID
 * environment variables.
 */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             /* getopt */
#include <mpich/mpi.h>

#include "icc.h"

#define DEFAULTLEN 4096
#define ROOTRANK 0
#define PORTFILE "mpi.port"

int
fileread(char *filepath, char *str, size_t maxlength);
int
filewrite(char *filepath, char *str, size_t strlength);

void
usage(void)
{
  fprintf(stderr, "usage: testapp [--standby [--length=ARRAY_LENGTH]]\n");
  exit(EXIT_FAILURE);
}


int
main(int argc, char **argv)
{
  static int standby = 0;
  static unsigned long length = DEFAULTLEN;
  static struct option longopts[] = {
    { "length",  required_argument, NULL,      'l'},
    { "standby", no_argument,       &standby,   1 },
    { NULL,      0,                 NULL,       0 }
  };

  int ch;
  char *endptr;
  while ((ch = getopt_long(argc, argv, "l:s", longopts, NULL)) != -1)
    switch (ch) {
    case 'l':
      length = strtoul(optarg, &endptr, 0);
      if (errno != 0 || endptr == optarg || *endptr != '\0')
        exit(EXIT_FAILURE);
      break;
    case 's':
      standby = 1;
      break;
    case 0:
      continue;
    default:
      usage();
    }
  argc -= optind;
  argv += optind;

  MPI_Init(&argc, &argv);

  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  /* XX get hosts? or srun with remaining nodes/ntasks? */
  char procname[MPI_MAX_PROCESSOR_NAME];
  int len;
  MPI_Get_processor_name(procname, &len);

  struct icc_context *icc = NULL;
  icc_init(ICC_LOG_INFO, ICC_TYPE_MPI, &icc);
  assert(icc);

  int rc;
  char portname[MPI_MAX_PORT_NAME];
  MPI_Comm intracomm;
  MPI_Comm intercomm;

  if (standby) {
    if (rank == ROOTRANK) {
      MPI_Open_port(MPI_INFO_NULL, portname);
      printf("Opened port \"%s\"\n", portname);

      int rpcrc;
      uint32_t jobid, nnodes;
      char *slurm_jobid = getenv("SLURM_JOBID");
      char *slurm_nnodes = getenv("SLURM_NNODES");

      char *end;
      errno = 0;
      jobid = strtoul(slurm_jobid, &end, 0);
      if (errno != 0 || end == slurm_jobid || *end != '\0')
        jobid = 0;

      errno = 0;
      nnodes = strtoul(slurm_nnodes, &end, 0);
      if (errno != 0 || end == slurm_nnodes || *end != '\0')
        nnodes = 0;

      rc = icc_rpc_malleability_avail(icc, "mpi", portname, jobid, nnodes, &rpcrc);
      assert(rc == ICC_SUCCESS && !rpcrc);
    }

    /* will block until a client connects to the port */
    MPI_Comm_accept(portname, MPI_INFO_NULL, ROOTRANK, MPI_COMM_WORLD, &intercomm);
  } else if (!standby) {

    /* there’s one communicator available for expansion, 0 merged */
    /* XX granularity is at the communicator level */
    /* XX rpcrc is not enough, we’d like a return struct */
    /* XX connect if malleability, make RPC! */
    if (rank == ROOTRANK) {
      rc = fileread(PORTFILE, portname, MPI_MAX_PORT_NAME);
      if (rc)
        exit(EXIT_FAILURE);
    }
    /* XX timeout! in info? */
    MPI_Comm_connect(portname, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &intercomm);
  }

  /* merge intercomm, ordering groups so that the root rank is not in
     the "standby" group */
  MPI_Intercomm_merge(intercomm, standby ? 1 : 0, &intracomm);

  /* get post merge ranks */
  MPI_Comm_rank(intracomm, &rank);
  MPI_Comm_size(intracomm, &nprocs);

  /* initialize random array with 0-255 value */
  int *data = malloc(length * sizeof(int));
  if (!data)
    exit(EXIT_FAILURE);

  if(rank == ROOTRANK) {
    for (unsigned int i = 0; i < length; i++)
      data[i] = rand() / (RAND_MAX / 256);
  }

  MPI_Bcast(data, length, MPI_INT, ROOTRANK, intracomm);

  int x = length / nprocs;   /* array size must be divisible by nprocs */
  int lo = rank * x;
  int hi = lo + x;
  int res = 0;
  for(int i = lo; i < hi; i++)
    res += data[i];

  printf("%d (%s): %d\n", rank, procname, res);

  /* compute global sum */
  int globalres = 0;
  MPI_Reduce(&res, &globalres, 1, MPI_INT, MPI_SUM, ROOTRANK, intracomm);

  if(rank == ROOTRANK) {
    printf("%d\n", globalres);
  }

  free(data);

  if (standby && rank == ROOTRANK)
    MPI_Close_port(portname);
  MPI_Comm_disconnect(&intracomm);

  icc_fini(icc);
  MPI_Finalize();
}


int
filewrite(char *filepath, char *str, size_t strlength) {
  int rc = 0;
  FILE *f = fopen(filepath, "w");

  if (f == NULL) {
    fprintf(stderr, "Could not open file \"%s\": %s\n", filepath ? filepath : "(NULL)", strerror(errno));
    return -1;
  }

  int nbytes = fprintf(f, "%s", str);
  if (nbytes < 0 || (unsigned)nbytes != strlength) {
    fprintf(stderr, "Could not write to file \"%s\": %s\n", filepath ? filepath : "(NULL)", strerror(errno));
    rc = -1;
  }

  fclose(f);
  return rc;
}


int
fileread(char *filepath, char *str, size_t maxlength) {
  int rc = 0;

  FILE *f = fopen(filepath, "r");
  if (!f) {
    fprintf(stderr, "Could not open file \"%s\": %s\n", filepath ? filepath : "(NULL)", strerror(errno));
    return -1;
  }

  if (!fgets(str, maxlength, f)) {
    fprintf(stderr, "Could not read from file \"%s\": %s\n", filepath ? filepath : "(NULL)", strerror(errno));
    rc = -1;
  }

  fclose(f);
  return rc;
}
