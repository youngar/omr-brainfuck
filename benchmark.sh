#! /bin/bash

set -o pipefail

# Global variables
bg_bench_pid=
start_time=

# collect_stats pid logname
collect_stats() {
    local pid=$1
    local logname=$2
    while ps -q $pid --no-headers -o '%cpu vsz' | tr -d '\n' >> $logname; do 
        local current_time=$(date +"%s.%3N")
        local temp=0$(echo "scale=3; $current_time - $start_time" | bc)
        echo " $temp"  >> $logname
        sleep 0.2
    done
}

# bench program logname
bench() {
    local program=$1
    local logname=$2

    # ${program} >/dev/null 2>&1 &
    ${program} &
    local pid=$!

    collect_stats $pid $logname
}

# bg_bench program logname
bg_bench(){
    local program=$1
    local logname=$2

    # ${program} >/dev/null 2>&1 &
    ${program} &
    bg_bench_pid=$!

    collect_stats $bg_bench_pid $logname &
}

bf=./bf
imperium=./omr/imperium/server/imperium_server
bf_program=../brainfuck/programs/mandelbrot.bf
script_dir="$(dirname $0)"

# Delete all the old files
rm -f \
client_only.log client_only.png \
imperium_client.log imperium_client.png \
imperium_server.log imperium_server.png \
imperium_client2.log imperium_client2.png \
imperium_server2.log imperium_server2.png 

# Run without the server
echo Running Client Only

start_time=$(date +"%s.%3N")

bench "$bf $bf_program" client_only.log

gnuplot -c $script_dir/plot.gp client_only.log client_only.png 


# Run with the server
echo Running Client+Server
echo Launching the server

start_time=$(date +"%s.%3N")
bg_bench "$imperium" imperium_server.log

# Must make sure the server is running before starting the client
sleep 2

echo Launching the client

bench "$bf --server 127.0.0.1:50055 $bf_program" imperium_client.log

kill "$bg_bench_pid"

gnuplot -c $script_dir/plot.gp imperium_client.log imperium_client.png 
gnuplot -c $script_dir/plot.gp imperium_server.log imperium_server.png 


# Run the client on the server twice in a row (caching)
echo Running Client+Server
echo Launching the server

start_time=$(date +"%s.%3N")

bg_bench "$imperium" imperium_server2.log

# Must make sure the server is running before starting the client
sleep 2

echo Launching the client

bench "$bf --server 127.0.0.1:50055 $bf_program" imperium_client2.log

echo Launching the client

bench "$bf --server 127.0.0.1:50055 $bf_program" imperium_client2.log

kill "$bg_bench_pid"

gnuplot -c $script_dir/plot.gp imperium_client2.log imperium_client2.png 
gnuplot -c $script_dir/plot.gp imperium_server2.log imperium_server2.png 
