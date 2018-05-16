#!/bin/sh

set -eux

(

    for threads in 1 2 4 8 12; do
        for lists in 1 4 8 16; do
            ./lab2_list --lists "$lists" --threads "$threads" --iterations 1000 --sync s
        done
    done

) > lab2b_5.csv
