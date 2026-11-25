#!/bin/sh

swig -I../C++ -D__cpp_noexcept_function_type -DHAVE_SSL=1 -DHAVE_MYSQL -DHAVE_POSTGRESQL -ruby -c++ -o QuickfixRuby.cpp quickfix.i

# SWIG's Ruby directors pass a callback's arguments to Ruby in an array from
# new[], and delete it once the call has returned. Give each director the array
# on its own stack instead, which needs no freeing however the director is left.
# Known to need it: SWIG 4.3.0, whose every array is one line
# `args.argv = new VALUE[n];` followed, before the next, by one line
# `delete [] args.argv;`. That form is rewritten. A wrapper whose directors use
# no such array is accepted as it is; any other form fails, so that a SWIG which
# changes this text is read before its output is accepted.
python3 - "$(swig -version | sed -n 's/^SWIG Version //p')" <<'EOF' || exit 1
import re
import sys

version = sys.argv[1]
allocation = re.compile(r'^(\s*)args\.argv = new VALUE\[([1-9][0-9]*)\];$')
deletion = re.compile(r'^\s*delete \[\] args\.argv;$')
assignment = re.compile(r'\bargs\.argv\s*=(?!=)\s*(.*)$')


def fail(why):
    sys.exit(f"swig.sh: SWIG {version}'s Ruby directors: {why} -- read the generated directors and update this script")


with open('QuickfixRuby.cpp') as f:
    lines = f.read().split('\n')
out = []
held = None
sites = 0
for number, line in enumerate(lines, 1):
    allocated = allocation.match(line)
    if allocated:
        if held:
            fail(f'line {number} makes an argument array while the one from line {held} is not deleted')
        out.append(f'{allocated.group(1)}VALUE quickfixArgv[{allocated.group(2)}]; args.argv = quickfixArgv;')
        held = number
        sites += 1
    elif deletion.match(line):
        if not held:
            fail(f'line {number} deletes an argument array that was not made')
        held = None
    else:
        out.append(line)
if held:
    fail(f'the argument array from line {held} is never deleted')

for number, line in enumerate(out, 1):
    assigned = assignment.search(line)
    if assigned and assigned.group(1) not in ('0;', 'quickfixArgv;'):
        fail(f'line {number} of the result gives args.argv an unknown value: {line.strip()}')
    if re.search(r'\bnew\s+VALUE\b|\bdelete\b.*\bargv\b|\bALLOC_N\s*\(\s*VALUE\b', line):
        fail(f'line {number} of the result still allocates or frees an array of VALUE: {line.strip()}')

if sites:
    with open('QuickfixRuby.cpp', 'w') as f:
        f.write('\n'.join(out))
else:
    print(f"swig.sh: SWIG {version}'s Ruby directors make no argument array on the heap; nothing to rewrite")
EOF
