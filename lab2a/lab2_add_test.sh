#!/bin/sh

rm -f ./lab2_add.csv

set -eux

# No sync
for iterations in 10 20 40 80 100 1000 5000 10000 20000 100000; do
    for threads in 1 2 4 8 12; do
        for yieldopt in '' '--yield'; do
            ./lab2_add --iterations $iterations --threads $threads $yieldopt >> ./lab2_add.csv
        done
    done
done

# With yield, sync
for threads in 2 4 8 12; do
    for sync in m s c; do
        iterations=$([ $sync = s ] && echo 1000 || echo 10000)
        ./lab2_add --yield --iterations $iterations --threads $threads --sync $sync >> ./lab2_add.csv
    done
done

# Without yield, sync
for threads in 1 2 4 8 12; do
    for syncopt in '' '--sync=s' '--sync=m' '--sync=c'; do
        ./lab2_add --iterations 10000 --threads $threads $syncopt >> ./lab2_add.csv
    done
done
