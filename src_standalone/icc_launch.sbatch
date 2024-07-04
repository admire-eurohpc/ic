#!/bin/bash
#SBATCH -e log-launch-%j.err
#SBATCH -o log-launch-%j.out
#SBATCH --job-name=icc-launch
#SBATCH --nodes=1

# setup environment
export RUN_PATH=/beegfs/home/j.fernandez/
export PATH=${RUN_PATH}/.local/bin:$PATH
export LD_LIBRARY_PATH=${RUN_PATH}/.local/lib:$LD_LIBRARY_PATH

redis-server --protected-mode no &
sleep 1
redis-cli flushall

icc_server
