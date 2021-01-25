#!/bin/bash
#BSUB -W 10
#BSUB -P csc357
#BSUB -nnodes 1
#BSUB -J jacobi3d-ampi-n1

# These need to be changed between submissions
file=jacobi3d
n_nodes=1
n_procs=$((n_nodes * 6))
grid_width=1536
grid_height=1536
grid_depth=1536

# Function to display commands
exe() { echo "\$ $@" ; "$@" ; }

cd $HOME/work/charm-inter/examples/ampi/cuda/gpudirect/jacobi3d

export LD_LIBRARY_PATH=$HOME/work/ucx-1.9.0/install/lib:$HOME/work/pmix-3.1.5/install/lib:/sw/summit/gdrcopy/2.0/lib64:$LD_LIBRARY_PATH

ppn=1
pemap="L0,4,8,84,88,92"
n_iters=100
warmup_iters=10

echo "# AMPI Jacobi3D Performance Benchmarking (GPUDirect off)"

for iter in 1 2 3
do
  date
  echo "# Run $iter"
  exe jsrun -n$n_procs -a1 -c$ppn -g1 -K3 -r6 --smpiargs="-disable_gpu_hooks" ./$file -x $grid_width -y $grid_height -z $grid_depth -w $warmup_iters -i $n_iters +ppn $ppn +pemap $pemap
done

echo "# AMPI Jacobi3D Performance Benchmarking (GPUDirect on)"

for iter in 1 2 3
do
  date
  echo "# Run $iter"
  exe jsrun -n$n_procs -a1 -c$ppn -g1 -K3 -r6 --smpiargs="-disable_gpu_hooks" ./$file -x $grid_width -y $grid_height -z $grid_depth -w $warmup_iters -i $n_iters +ppn $ppn +pemap $pemap -d
done
