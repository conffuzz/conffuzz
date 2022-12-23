#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

benchmark_web_server() {
  for i in $( eval echo {0..${3}} )
  do
    # start whatever web server
    $1 &> /tmp/app.out

    # measure it
    wrk -t 10 -c 20 -d 20s \
        http://localhost | \
        awk 'BEGIN{a["Requests/sec:"]} ($1 in a) && ($2 ~ /[0-9]/){print $2}'

    # cleanup
    $2
  done
}

_benchmark_command() {
  # grep for real time
  { time $1; } 2>&1 | grep -Po "\d+\.\d+s$" | head -1 | rev | cut -c 2- | rev
}

benchmark_command() {
  for i in $( eval echo {0..${2}} )
  do
    { _benchmark_command $1; } &
    pid=$!
    sleep 3
    pkill -9 $pid
    wait $pid
  done
}
