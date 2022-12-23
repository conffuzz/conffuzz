#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

# Given a symbol name and a library path as arguments, this script returns
# the API signature of the symbol on stdout. Errors, information, and warnings,
# are all returned via stderr.
#
# Format of the output:
# [symbol name] [symbol name in DWARF] [n# of arguments] [return value type] { [arg #0 type] ... }
#
# For example:
#
#   $ ./conffuzz/analyze-symbol.sh mod_deflate_cache_file_name /usr/local/lib/mod_deflate.so
#   mod_deflate_cache_file_name mod_deflate_cache_file_name 3 buffer* request_st* buffer* buffer*
#
# In this case mod_deflate_cache_file_name has a return value of type buffer *,
# with three arguments of types request_st*, buffer*, and buffer*.
#
#   $ ./conffuzz/analyze-symbol.sh aesni_cbc_encrypt /usr/local/lib/libcrypto.so.1.1
#   aesni_cbc_encrypt aesni_cbc_encrypt 6 unknown_or_void unsignedchar* unsignedchar* size_t AES_KEY* unsignedchar* int
#
# etc.

DWARFDUMP=llvm-dwarfdump-11

SYMBOL=$1
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

# sometimes we can have twice the same entry, once for the .h, once for the .c
# and sed "/0x[0-9a-z]*:\s*NULL/q" allows us to stop at the first one
# note the --recurse-depth=1: we only want to catch DW_TAG_formal_parameter
# one level below, otherwise we would also catch them for potentially inlined
# functions which would mess up our argument numbers
dwarfentry=$($DWARFDUMP $LIBPATH -n $SYMBOL -c --recurse-depth=1 | \
	                                 sed "/0x[0-9a-z]*:\s*NULL/q")
# probably not a valid symbol name
if [ -z "$dwarfentry" ]; then
    >&2 echo "[E] Not able to find symbol $SYMBOL in DWARF"
    exit 1
fi

narg=$(echo "$dwarfentry" | grep DW_TAG_formal_parameter | wc -l)

# we need to use linkage name in the case of C++
symbolname=$(echo "$dwarfentry" | sed -nr 's/\s*DW_AT_name\s*\(\"([^ ]*)\"\)/\1/p' | head -1)
if [ -z "$symbolname" ]; then
    >&2 echo "[E] Not able to find unmangled symbol name for $SYMBOL in DWARF"
    echo -n "$SYMBOL $SYMBOL $narg"
else
    echo -n "$symbolname $SYMBOL $narg"
fi

CONST_PREFIX="const "
STATIC_PREFIX="static "
CONSTSTATIC_PREFIX="const static "
STATICCONST_PREFIX="static const "
CONSTCONST_PREFIX="const const "
CONSTCONSTSTATIC_PREFIX="const const static"
STATICCONSTCONST_PREFIX="static const const"

# grab return value type
wrote=0
while read -r line
do
    # if we reach parameters, it means that we didn't find
    # the return value type of the function
    if [[ $line == *"DW_TAG_formal_parameter"* ]]; then
        break
    fi
    if [[ $line == *"DW_AT_type"* ]]; then
        wrote=1
        # note that the ordering of the seds is important
        type=$(echo "$line" | grep -Po "\"\K(.*)(?=\"\))" \
                            | sed -e "s/^$STATICCONSTCONST_PREFIX//" \
                            | sed -e "s/^$CONSTCONSTSTATIC_PREFIX//" \
                            | sed -e "s/^$STATICCONST_PREFIX//" \
                            | sed -e "s/^$CONSTCONST_PREFIX//" \
                            | sed -e "s/^$CONSTSTATIC_PREFIX//" \
                            | sed -e "s/^$CONST_PREFIX//" \
                            | sed -e "s/^$STATIC_PREFIX//" \
                            | tr -d ' ')
        echo -n " $type"
	# there is only one return value, so we can safely exit now
        break
    fi
done < <(echo "$dwarfentry")

# if we didn'd find the return value type, say it.
if [ "$wrote" -eq "0" ]; then
   echo -n " unknown_or_void";
fi

# grab argument types
in_param=0
wrote=0
while read -r line
do
    if [[ $line == *"DW_TAG_formal_parameter"* ]]; then
        in_param=1
        continue
    fi
    if [[ $line == "" ]]; then
        if [[ $in_param -eq 1 && $wrote -eq 0 ]]; then
            echo -n " char"
        fi
        in_param=0
        wrote=0
        continue
    fi
    if [[ $in_param -eq 1 && $line == *"DW_AT_type"* ]]; then
        wrote=1
        type=$(echo "$line" | grep -Po "\"\K(.*)(?=\"\))" \
                            | sed -e "s/^$STATICCONSTCONST_PREFIX//" \
                            | sed -e "s/^$CONSTCONSTSTATIC_PREFIX//" \
                            | sed -e "s/^$STATICCONST_PREFIX//" \
                            | sed -e "s/^$CONSTCONST_PREFIX//" \
                            | sed -e "s/^$CONSTSTATIC_PREFIX//" \
                            | sed -e "s/^$CONST_PREFIX//" \
                            | sed -e "s/^$STATIC_PREFIX//" \
                            | tr -d ' ')
        echo -n " $type"
        continue
    fi
done < <(echo "$dwarfentry")

# add missing newline
echo ""
