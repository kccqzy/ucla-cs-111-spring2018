#!/bin/sh

rm -f ./lab2_list.csv

set -eux

# Single-threaded runs
for iterations in 10 100 1000 10000 20000; do
    ./lab2_list --iterations $iterations >> ./lab2_list.csv
done

# Problematic runs (multi-threaded without sync)
for yieldopt in i d il dl; do
    until ./lab2_list --iterations 4 --threads 2 --yield $yieldopt >> ./lab2_list.csv; do
        echo "Failed, retrying..." >&2
    done
done

# Good runs without yield
for threads in 1 2 4 8 12 16 24; do
    for sync in s m; do
        ./lab2_list --iterations 1000 --threads $threads --sync $sync >> ./lab2_list.csv
    done
done

# Good runs with yield
for yieldopt in i d il dl; do
    for threads in 2 4 8 12; do
        for iterations in 4 8 16 32; do
            for sync in s m; do
                ./lab2_list --yield $yieldopt --iterations $iterations --threads $threads --sync $sync >> ./lab2_list.csv
            done
        done
    done
done
