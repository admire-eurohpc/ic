#!/bin/sh

unset SLURM_JOB_NODELIST

exec mpiexec $@
