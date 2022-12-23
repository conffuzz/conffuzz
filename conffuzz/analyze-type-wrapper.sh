#!/bin/bash

# set -x

# This script wraps analyze-type.sh for the instrumentation. It outputs the
# results to the scratch file, and updates the global type DB so that future
# runs get this type information at startup.

# input
TYPE_DB_PATH=$1
OUT_PATH=$2
TYPE=$3
#  4. Paths of the libraries that may define the symbol
#     See (x)

# constants
BASEDIR=$(dirname "$0")
TYPE_ANALYZER=${BASEDIR}/analyze-type.sh
TMP=/tmp/analyze-type-wrapper.sh.tmp

# cleanup potential stale runs
rm -rf $TMP

# run analysis
# (x) iterate over libs passed
for l in "${@:4}"
do
    $TYPE_ANALYZER $TYPE $l >> $TMP
done

# remove trailing spaces / new lines and deduplicate
cat $TMP | sed "s/\n\n/\n/g" | sort -uk1,1 | sponge $TMP

# update the type database so that future workers have this type at startup
cat $TMP >> $TYPE_DB_PATH

# output to worker scratch (used by the instrumentation)
cat $TMP | cut -f2 -d" " >> $OUT_PATH

# cleanup after ourselves
rm -rf $TMP
