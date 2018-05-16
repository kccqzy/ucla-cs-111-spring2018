#! /usr/bin/env gnuplot

# general plot parameters
set terminal png
set datafile separator ","

set title "Figure-4: Aggregated throughput using sublists, with spinlocks"
set xlabel "Threads"
set logscale x 2
set ylabel "Operations per second"
set logscale y
set output 'lab2b_5.png'
set key left top
plot \
     "< grep -E -e 'list-none-s,[0-9]+,1000,1,' lab2b_5.csv" using ($2):(1000000000/($7)) \
	title '1 list' with linespoints lc rgb 'green', \
     "< grep -E -e 'list-none-s,[0-9]+,1000,4,' lab2b_5.csv" using ($2):(1000000000/($7)) \
	title '4 lists' with linespoints lc rgb 'blue', \
     "< grep -E -e 'list-none-s,[0-9]+,1000,8,' lab2b_5.csv" using ($2):(1000000000/($7)) \
	title '8 lists' with linespoints lc rgb 'orange', \
     "< grep -E -e 'list-none-s,[0-9]+,1000,16,' lab2b_5.csv" using ($2):(1000000000/($7)) \
	title '16 lists' with linespoints lc rgb 'violet'
