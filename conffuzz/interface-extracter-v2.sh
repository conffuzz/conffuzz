#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# This is a version of interface-extracter.sh that uses readelf. Ultimately
# it should probably replace it, but this is a big change that we don't want
# to make at this point.

BASEDIR=$(dirname "$0")
SYMBOL_ANALYZER=${BASEDIR}/analyze-symbol.sh

OBJDUMP=readelf

REGEX=$1
LIBPATH=$2

if [ -z "$LIBPATH" ]; then
    >&2 echo "[E] Argument 2 (library path) of this script cannot be empty"
    exit 1
fi

# check that commands exist
# important: echo to stderr in case of error

if ! command -v $OBJDUMP &> /dev/null
then
    >&2 echo "[E] $OBJDUMP not found, install it, or edit its name in $0"
    exit 1
fi

# extract symbols (mangled in the C++ case)
if [ -z "$REGEX" ]; then
    symbols=$($OBJDUMP --wide -s $LIBPATH | grep "FUNC" | awk '{ print $8 }')
else
    symbols=$($OBJDUMP --wide -s $LIBPATH | grep "FUNC" | awk '{ print $8 }' \
                                   | grep -P "$REGEX")
fi

for symbol in $symbols
do
    $SYMBOL_ANALYZER $symbol $LIBPATH
done
