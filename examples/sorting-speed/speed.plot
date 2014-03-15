
# IMPORT-DATA stats stats.txt

set terminal pdf size 28cm,18cm linewidth 2.0
set output "speed.pdf"

set pointsize 0.7
set style line 6 lc rgb "#f0b000"
set style line 15 lc rgb "#f0b000"
set style line 24 lc rgb "#f0b000"
set style line 33 lc rgb "#f0b000"
set style line 42 lc rgb "#f0b000"
set style line 51 lc rgb "#f0b000"
set style line 60 lc rgb "#f0b000"
set style increment user

set grid xtics ytics

set key top left

set title 'Simple C++ Sorting Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Run Time per Item [Nanoseconds / Item]'

## MULTIPLOT(algo) SELECT LOG(2, size) AS x, MEDIAN(time / repeats / size * 1e9) AS y, MULTIPLOT
## FROM stats GROUP BY MULTIPLOT,x ORDER BY MULTIPLOT,x
plot \
    'speed-data.txt' index 0 title "algo=std::heap_sort" with linespoints, \
    'speed-data.txt' index 1 title "algo=std::sort" with linespoints, \
    'speed-data.txt' index 2 title "algo=std::stable_sort" with linespoints


