#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

ITERATIONS=10

REDIS=/root/redis-6.2.6/src/redis-server

PID=0

benchmark_redis() {
  for i in $( eval echo {0..${3}} )
  do
    # start
    $1 &> /tmp/app.out

    # measure it
    redis-benchmark \
            -h localhost -p 6379 \
            -n 100000 \
            --csv \
            -q \
            -c 30 \
            -k 1 \
            -P 16 \
            -t get \
            -d 10 | grep -Po "\d+\.\d+"

    # cleanup
    $2
  done
}

run_redis_instrumented() {
  ./pintools/pin -t ./conffuzz/instrumentation.so \
      -noMonitor "1" \
      -symbols /root/functions-get.txt \
      -typesPath /root/types-get.txt \
      -symboltool "/root/conffuzz/interface-extracter.sh" \
      -typetool "/root/conffuzz/analyze-type-wrapper.sh" \
      -fifoMonitor "" \
      -fifoWorker "" \
      -Verbose "0" \
      -instrRetCB "1" \
      -o "/tmp/instr.out" -- \
      ${REDIS} /root/redis-6.2.6/redis.conf & disown
  PID=$!
  sleep 3 # wait for it to start
}

run_redis_vanilla_chocolate() {
  ${REDIS} /root/redis-6.2.6/redis.conf & disown
  PID=$!
  sleep 3 # wait for it to start
}

kill_redis_nice_and_proper() {
  kill -9 $PID &> /dev/null
  wait $PID
  pkill -9 redis-benchmark &> /dev/null
  sleep 1 # wait a bit more to ensure that the server is really dead
}

echo "getCommand getCommand 1 unknown_or_void client*" > /root/functions-get.txt
echo "client 16984" > /root/types-get.txt

echo "Benchmarking Redis instrumented (this will take a while)"
benchmark_redis run_redis_instrumented kill_redis_nice_and_proper \
  $ITERATIONS | tee /tmp/results

instrumented_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${instrumented_perf}"

echo "Benchmarking Redis vanilla (this will take a while)"
benchmark_redis run_redis_vanilla_chocolate kill_redis_nice_and_proper \
  $ITERATIONS | tee /tmp/results

vanilla_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${vanilla_perf}"

echo "AVERAGE OVERHEAD: $(bc <<< "100-(100*${instrumented_perf}/${vanilla_perf})")%"
