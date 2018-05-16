#! /usr/bin/env gnuplot

# general plot parameters
set terminal png
set datafile separator ","

set title "Figure-2: Mutex Wait Time"
set xlabel "Threads"
set logscale x 2
set ylabel "Average time per operation (ns)"
set logscale y
set output 'lab2b_2.png'
set key left top
plot \
     "< grep -e 'list-none-m' lab2b_1.csv" using ($2):($8) \
	title 'Wait-for-lock time per operation' with linespoints lc rgb 'blue', \
     "< grep -e 'list-none-m' lab2b_1.csv" using ($2):($7) \
	title 'Time per operation' with linespoints lc rgb 'green'
