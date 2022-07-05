#!/bin/sh

# inject the --exact flag is necessary for parallel job steps or
# spawned MPI processes.

exec srun --exact $@
