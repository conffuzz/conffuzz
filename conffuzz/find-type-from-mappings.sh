#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

BASEDIR=$(dirname "$0")
ANALYZE_TYPE=${BASEDIR}/analyze-type.sh

TMP_STDERR=/tmp/find-type-from-mappings.stderr.tmp
TMP_STDOUT=/tmp/find-type-from-mappings.stdout.tmp

TYPE=$1
MAPPINGS=$2

if [ -z "$MAPPINGS" ]; then
    >&2 echo "[E] Argument 2 (mappings) of this script cannot be empty"
    exit 1
fi

# retrieve the list of mapped libs in the address space
mapped=$(cat $MAPPINGS | awk '{ print $NF }' | sed 's/^[0-9]*$//g' | \
         sed '/[\[]/d' | sed '/^[[:space:]]*$/d' | sort -u)

# note: we take the first match. If there are conflicts, well, hope
# it's the right one :-)
for lib in $mapped
do
    # I do not expect libasan to ever be the right library to get
    # the definition from
    if [[ $lib == *"libasan."* ]]; then
        continue
    fi
    $ANALYZE_TYPE $TYPE $lib > $TMP_STDOUT 2> $TMP_STDERR
    if [ -s "$TMP_STDOUT" ];
    then
        # found it
	cat $TMP_STDOUT
        break
    else
        rm -rf $TMP_STDOUT $TMP_STDERR
    fi
done

rm -rf $TMP_STDOUT $TMP_STDERR
