#!/bin/sh
# Run the ICC server for 10 minutes. The address file will be read
# in the ADMIRE_DIR directory.

#SBATCH --job-name=admire_icc_server
#SBATCH --output=icc_client_out.txt
#SBATCH --time=00:00:30
#SBATCH --ntasks=1

case `hostname` in
    *"plafrim.cluster")
        default_dir=/projets/admire
        path=local/bin
        libpath=local/lib
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

srun --time=00:00:30 icc_client
