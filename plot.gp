# input parameters:
#   data_name
#   output_name

set term png small size 800,600

set output ARG2

set ylabel "% CPU"
set ytics nomirror
set yrange [0:*]
set format y '%.2f%%'

set y2label "Memory"
set y2tics nomirror in
set y2range [0:*]
set format y2 '%.0s%cB'

set xdata time
set timefmt "%s"
set format x "%s"

set style data lines

plot ARG1 using 3:1 with lines axes x1y1 title "%CPU", \
     ARG1 using 3:2 with lines axes x1y2 title "Memory"