#!/usr/bin/python3
# SPDX-License-Identifier: BSD-3-Clause
# Authors: Vlad-Andrei Badoiu <vlad_andrei.badoiu@upb.ro>
#          Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>

import angr
import sys
import re
from time import sleep
import contextlib
import argparse

# This script uses static binary analysis to extract callees count for a given API
# along with the number of unique API endpoints called.
#
# Dependencies:
#   `pip3 install angr`
#
# Running:
#   `python3 static-analyze-entry-points.py -f $functions_file -b $binary`

# Writes to stdout the callee count
parser = argparse.ArgumentParser(description='Binary static analysis')
parser.add_argument('-b', '--binary', dest='binary', help='Path to binary', required=True)
parser.add_argument('-f','--funcsfile', dest='funcsfile', help='Path to functions file',
                    required=True)

args = parser.parse_args()

p = angr.Project(args.binary, load_options={'auto_load_libs': False})

# get list of functions from functions file
FUNCS = []
with open(args.funcsfile) as f:
    for line in f:
        match = re.search('[^\s]+ ([^\s]+) .*', line)
        FUNCS.append(match.group(1))

with contextlib.redirect_stdout(None):
    cfg = p.analyses.CFGFast()

nodes = list(cfg.graph.nodes(data=True))
api_callee_count = 0
res = {}

used = set()

for idx, i in enumerate(nodes):
    #print(nodes[0:], file=sys.stderr)
    # Should use .startsWith? for jumps inside the func?
    if i[0]._name != None and i[0]._name in FUNCS:
        res = i[0].predecessors_and_jumpkinds()
        # PLT case
        if (len(res) == 1 and res[0][1] == 'Ijk_Boring'):
            #print(res[0][0].predecessors)
            api_callee_count += len(res[0][0].predecessors)
        else:
            api_callee_count += len(res)
        used.add(i[0]._name)

#print("##### Results static binary ######")
#print(res)
#print("Static analysis unique API callee count {}".format(api_calee_count))

print(api_callee_count)
print(len(used))

# Symbolic execution
"""
main = p.loader.main_object.get_symbol("main")
start_state = p.factory.blank_state(addr=main.rebased_addr)
start_state.stack_push(0x0)

cfg_symbolic = p.analyses.CFGEmulated(fail_fast=False, starts=[main.rebased_addr], context_sensitivity_level=1, enable_function_hints=False, keep_state=False)


nodes = list(cfg_symbolic.graph.nodes(data=True))
api_callee_count = 0
res = {}

for api_func in FUNCS:
    print(nodes[0:])
    for idx, i in enumerate(nodes):
        # Should use .startsWith? for jumps inside the func?
        if i[0]._name == api_func:
            #print(i[0].predecessors_and_jumpkinds())
            print(i[0].predecessors_and_jumpkinds())
            res[api_func] = i[0].predecessors_and_jumpkinds()
            api_callee_count += len(i[0].predecessors_and_jumpkinds())

print("##### Results symbolic binary ######")
print(res)
print("Symbolic execution unique API callee count {}".format(api_calee_count))
"""
