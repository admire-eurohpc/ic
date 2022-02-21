#!/bin/sh

ADMIRE_JOB_ENV="admire_job.env"

if [ ! -z $SLURM_JOB_ID ]; then
    ADMIRE_JOB_ID=$SLURM_JOB_ID
elif [ ! -z $SLURM_JOBID ]; then
    ADMIRE_JOB_ID=$SLURM_JOBID
else
    ADMIRE_JOB_ID=-1
fi

if [ ! -z $SLURM_JOB_NUM_NODES ]; then
    ADMIRE_JOB_NNODES=$SLURM_JOB_NUM_NODES
elif [ ! -z $SLURM_NNODES ]; then
    ADMIRE_JOB_NNODES=$SLURM_NNODES
else
    ADMIRE_JOB_NNODES=-1
fi

if [ ! -z $SLURM_JOB_NTASKS ]; then
    ADMIRE_JOB_NTASKS=$SLURM_JOB_NTASKS
elif [ ! -z $SLURM_NNODES ]; then
    ADMIRE_JOB_NTASKS=$SLURM_NPROCS
else
    ADMIRE_JOB_NTASKS=-1
fi

# if run in a Slurm prolog, this will set the tasks env
export ADMIRE_JOB_ID ADMIRE_JOB_NNODES ADMIRE_JOB_NTASKS

# otherwise, env will get picked up from the file ADMIRE_JOB_ENV
cat << EOF >| $ADMIRE_JOB_ENV
ADMIRE_JOB_ID=$ADMIRE_JOB_ID
ADMIRE_JOB_NNODES=$ADMIRE_JOB_NNODES
ADMIRE_JOB_NTASKS=$ADMIRE_JOB_NTASKS
EOF
