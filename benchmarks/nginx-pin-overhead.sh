#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# defines benchmark_web_server
source benchmarks/common.sh

ITERATIONS=10

NGINX=/root/nginx-1.21.6/objs/nginx

PID=0

run_nginx_instrumented() {
  ./pintools/pin -t ./conffuzz/instrumentation.so \
      -noMonitor "1" \
      -symbols /root/api/libpcre2/functions.txt \
      -typesPath /root/api/libpcre2/types.txt \
      -symboltool "/root/conffuzz/interface-extracter.sh" \
      -typetool "/root/conffuzz/analyze-type-wrapper.sh" \
      -fifoMonitor "" \
      -fifoWorker "" \
      -Verbose "0" \
      -instrRetCB "1" \
      -o "/tmp/instr.out" -- \
      ${NGINX} \
      -g 'daemon off; error_log stderr debug; master_process off;' & disown
  PID=$!
  sleep 3 # wait for it to start
}

run_nginx_vanilla_chocolate() {
  $NGINX -g 'daemon off; error_log stderr debug; master_process off;' & disown
  PID=$!
  sleep 3 # wait for it to start
}

kill_nginx_nice_and_proper() {
  kill -9 $PID &> /dev/null
  wait $PID
  pkill -9 wrk &> /dev/null
  sleep 1 # wait a bit more to ensure that the server is really dead
}

echo "Benchmarking Nginx instrumented (this will take a while)"
benchmark_web_server run_nginx_instrumented kill_nginx_nice_and_proper \
  $ITERATIONS | tee /tmp/results

instrumented_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${instrumented_perf}"

echo "Benchmarking Nginx vanilla (this will take a while)"
benchmark_web_server run_nginx_vanilla_chocolate kill_nginx_nice_and_proper \
  $ITERATIONS | tee /tmp/results

vanilla_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${vanilla_perf}"

echo "AVERAGE OVERHEAD: $(bc <<< "100-(100*${instrumented_perf}/${vanilla_perf})")%"
