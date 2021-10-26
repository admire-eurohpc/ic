#!/bin/sh
# Run the ICC server for 10 minutes. The address file will be written
# in the IC_RUNTIME_DIR directory.

#SBATCH --job-name=admire_icc_server
#SBATCH --output=icc_server_out.txt
#SBATCH --time=00:10:00
#SBATCH --ntasks=1

ADMIRE_DIR=/projets/admire

PATH=$ADMIRE_DIR/local/bin:$PATH
LD_LIBRARY_PATH=$ADMIRE_DIR/local/lib
IC_RUNTIME_DIR=$ADMIRE_DIR/icc

export PATH LD_LIBRARY_PATH IC_RUNTIME_DIR 

mkdir -p $IC_RUNTIME_DIR

srun icc_server

