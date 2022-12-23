#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# defines benchmark_command
source benchmarks/common.sh

export "ASAN_OPTIONS=detect_leaks=0 detect_odr_violation=0"

ITERATIONS=10

XMLTEST=/root/libxml2-2.10.3/.libs/testapi

run_xmltest_instrumented() {
  ./pintools/pin -t ./conffuzz/instrumentation.so \
      -noMonitor "1" \
      -symbols /tmp/conffuzz_functions.txt \
      -typesPath /tmp/conffuzz_types.txt \
      -symboltool "/root/conffuzz/interface-extracter.sh" \
      -typetool "/root/conffuzz/analyze-type-wrapper.sh" \
      -fifoMonitor "" \
      -fifoWorker "" \
      -Verbose "0" \
      -instrRetCB "1" \
      -o "/tmp/instr.out" -- ${XMLTEST}
}

# generate api files
./conffuzz/conffuzz -X /usr/local/lib/libxml2.so.2.10.3 -r "^xmlTextWriter.*" ${XMLTEST}

echo "Benchmarking xmltest instrumented (this will take a while)"
benchmark_command run_xmltest_instrumented $ITERATIONS | tee /tmp/results

instrumented_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${instrumented_perf}"

echo "Benchmarking xmltest vanilla (this will take a while)"
benchmark_command $XMLTEST $ITERATIONS | tee /tmp/results

vanilla_perf=$(datamash mean 1 < /tmp/results)

echo "AVERAGE: ${vanilla_perf}"

echo "AVERAGE OVERHEAD: $(bc <<< "${vanilla_perf}/${instrumented_perf}")x"
