#!/bin/sh
# Run Redis server and the ICC server for 10 minutes. The ICC address
# file will be written in the ADMIRE_DIR directory.

#SBATCH --job-name=admire_ic
#SBATCH --output=icc_server_out.txt
#SBATCH --ntasks=2

case `hostname` in
    *"plafrim.cluster")
        default_dir=/projets/admire
        path=$default_dir/local/bin
        libpath=$default_dir/local/lib
        ;;
    *)
        default_dir=/tmp
        path=~/.local/bin
        libpath=~/.local/lib
        ;;
esac

ADMIRE_DIR=${ADMIRE_DIR:-$default_dir}
PATH=$path:$PATH
LD_LIBRARY_PATH=$libpath

export PATH LD_LIBRARY_PATH ADMIRE_DIR

mkdir -p $ADMIRE_DIR

srun redis-server &
sleep 2				# give some time to Redis to start
srun icc_server
