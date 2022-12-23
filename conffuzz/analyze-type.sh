#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# Given a type name and a library path as arguments, this script returns
# the size of the type on stdout. Errors, information, and warnings, are
# all returned via stderr.
# 
# For example:
# 
#   $ ./conffuzz/analyze-type.sh int /usr/local/lib/libpcre2-8.so.0.10.4
#   int 4
#   $ ./conffuzz/analyze-type.sh pcre2_code_8 /usr/local/lib/libpcre2-8.so.0.10.4
#   [I] Recursing from pcre2_code_8 into pcre2_real_code_8
#   pcre2_code_8 136

DWARFDUMP=llvm-dwarfdump-11

TYPE=$1
LIBPATH=$2

if [ -z "$LIBPATH" ]; then
    >&2 echo "[E] Argument 2 (library path) of this script cannot be empty"
    exit 1
fi

# check that commands exist
# important: echo to stderr in case of error

if ! command -v $DWARFDUMP &> /dev/null
then
    >&2 echo "[E] $DWARFDUMP not found, install it, or edit its name in $0"
    exit 1
fi

get_type() {
    # periodic rain
    #   so no need to worry:
    #      rice sprouts
    # (Basho)

    # grab full DWARF record for this type; might be several entries
    dwarfentry=$($DWARFDUMP $LIBPATH --ignore-case -n "$1" -c)

    # probably not a valid symbol name
    if [ -z "$dwarfentry" ]; then
        >&2 echo "[E] Not able to find type $TYPE in DWARF"
        exit 1
    fi

    # get sorted list of sizes in decimal format
    #  e.g., DW_AT_byte_size (0x88)
    # note: `perl -lpe '$_=hex()'` converts from hex to dec.
    sizes=$(echo "$dwarfentry" | grep -Po "DW_AT_byte_size\s+\(0x\K(.*)(?=\))" \
                               | perl -lpe '$_=hex()' | sort)

    # did we find a size?
    if [ -n "$sizes" ]; then
        first_size=$(echo "$sizes" | head -1)
	echo "$TYPE $first_size"
	
        last_size=$(echo "$sizes" | tail -n 1)
	# still warn the user if the size is ambiguous
	if [ ! "$first_size" == "$last_size" ]; then
            >&2 echo "[W] Ambiguous size for $TYPE: [$first_size, $last_size]"
        fi
	exit 0
    fi

    # get a typedef entry for this name?
    dw_at_type=$(echo "$dwarfentry" | \
	         grep -Po "DW_AT_type\s+\(0x[0-9a-zA-Z]+ \"\K(.*)(?=\"\))" | \
		 head -1)

    # did we find a typedef entry?
    if [ -n "$dw_at_type" ]; then
        # is this typedef not the same? we don't want to recurse forever...
        if [ "$1" = "$dw_at_type" ]; then
            >&2 echo "[W] Detected infinite recursion with $dw_at_type, aborting"
        else
            # recurse with the value of DW_AT_type
            >&2 echo "[I] Recursing from $1 into $dw_at_type"
            get_type "$dw_at_type"
        fi
    fi

    # if this is an enumeration type that doesn't have a DWARF entry,
    # lookup the basis type (same size)
    basis_type=$(echo "$1" | sed 's/^enumeration //')
    if [ -n "$basis_type" ]; then
        if [ ! "$1" == "$basis_type" ]; then
            >&2 echo "[I] De-enumerating $1 into $basis_type"
            get_type "$basis_type"
        fi
    fi

    # we cannot find type size unfortunately
    >&2 echo "[E] Not able to find type information for $TYPE in DWARF"
    exit 1
}

get_type $TYPE
