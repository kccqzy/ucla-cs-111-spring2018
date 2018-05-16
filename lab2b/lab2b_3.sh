#!/bin/sh

set -eux

(

    for threads in 1 2 4 8 12 16; do
        for iterations in 1 2 4 8 16; do
            ./lab2_list --yield id --lists 4 --threads "$threads" --iterations "$iterations" || true
        done
    done

    for sync in s m; do
        for threads in 1 2 4 8 12 16; do
            for iterations in 10 20 40 80; do
                ./lab2_list --yield id --lists 4 --threads "$threads" --iterations "$iterations" --sync "$sync"
            done
        done
    done

) > lab2b_3.csv
