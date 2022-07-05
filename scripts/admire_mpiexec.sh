#!/bin/sh

# SLURM_JOB_NODELIST will cause trouble when mpiexec exec srun if the
# number of underlying nodes has changed
unset SLURM_JOB_NODELIST

BINDIR=$(dirname $(readlink -f "$0"))

exec mpiexec -launcher-exec=$BINDIR/admire_srun $@
