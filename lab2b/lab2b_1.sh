#!/bin/sh

set -eux

(

    for sync in m s; do
        for threads in 1 2 4 8 12 16 24; do
            ./lab2_list --sync "$sync" --iterations 1000 --threads "$threads"
        done
    done

) > lab2b_1.csv
