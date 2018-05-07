#!/bin/sh

set -eux

dd if=/dev/null of=./lab2_add.csv # truncate
make lab2_add

for iterations in 100 1000 10000 100000; do
    for threads in 2 4 8 12; do
        for yieldopt in '' '--yield'; do
            for syncopt in '' '--sync=m' '--sync=c'; do
                ./lab2_add --iterations $iterations --threads $threads $yieldopt $syncopt >> ./lab2_add.csv
            done
        done
    done
done

# spinlock special
for iterations in 100 1000 ; do
    for threads in 2 4; do
        for yieldopt in '' '--yield'; do
            ./lab2_add --iterations $iterations --threads $threads $yieldopt --sync=s >> ./lab2_add.csv
        done
    done
done
