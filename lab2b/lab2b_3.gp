#! /usr/bin/env gnuplot

# general plot parameters
set terminal png
set datafile separator ","

set title "Figure-3: Unprotected Threads  and Iterations that run without failure"
set xlabel "Threads"
set logscale x 2
set xrange [0.75:18]
set ylabel "Successful Iterations"
set logscale y
set output 'lab2b_3.png'
plot \
     "lab2b_3.csv" using ($2):($3) \
	title 'yield=id, 4 lists' with points lc rgb 'blue'
