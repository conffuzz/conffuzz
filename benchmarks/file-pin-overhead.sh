#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# defines benchmark_command
source benchmarks/common.sh

ITERATIONS=10

FILE=/usr/local/bin/file

run_file_instrumented() {
  ./pintools/pin -t ./conffuzz/instrumentation.so \
      -noMonitor "1" \
      -symbols /root/api/libmagic/functions.txt \
      -typesPath /root/api/libmagic/types.txt \
      -symboltool "/root/conffuzz/interface-extracter.sh" \
      -typetool "/root/conffuzz/analyze-type-wrapper.sh" \
      -fifoMonitor "" \
      -fifoWorker "" \
      -Verbose "0" \
      -instrRetCB "1" \
      -o "/tmp/instr.out" -- \
      ${FILE} \
      README.md /usr/lib/x86_64-linux-gnu/libmagic.so.1 /usr/local/bin/file
}

run_file_vanilla_chocolate() {
  $FILE README.md /usr/lib/x86_64-linux-gnu/libmagic.so.1 /usr/local/bin/file
}

echo "Benchmarking file instrumented (this will take a while)"
benchmark_command run_file_instrumented $ITERATIONS | tee /tmp/results

instrumented_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${instrumented_perf}"

echo "Benchmarking file vanilla (this will take a while)"
benchmark_command run_file_vanilla_chocolate $ITERATIONS | tee /tmp/results

vanilla_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${vanilla_perf}"

echo "AVERAGE OVERHEAD: $(bc <<< "100-(100*${vanilla_perf}/${instrumented_perf})")%"
