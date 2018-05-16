#! /usr/bin/env gnuplot

# general plot parameters
set terminal png
set datafile separator ","

set title "Figure-1: Aggregate throughput"
set xlabel "Threads"
set logscale x 2
set ylabel "Total number of operations per second"
set logscale y
set output 'lab2b_1.png'
set key left top
plot \
     "< grep -e 'list-none-m' lab2b_1.csv" using ($2):(1000000000/($7)) \
	title 'Mutex' with linespoints lc rgb 'blue', \
     "< grep -e 'list-none-s' lab2b_1.csv" using ($2):(1000000000/($7)) \
	title 'Spin-lock' with linespoints lc rgb 'green'
