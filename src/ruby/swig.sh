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

# SWIG's Ruby directors run each rescue's director:except body -- which hands the
# FIX exceptions the engine expects to it -- only inside a
# `<Class>_<method>_call_depth == 0` guard, and otherwise `rb_exc_raise(error)`.
# Make the body run at every depth: drop the guard, keep the balancing
# `<Class>_<method>_call_depth--`, and drop the trailing `rb_exc_raise`, so the
# body's `throw` reaches the engine from the director's own C++ frame, where
# rb_protect has already caught the Ruby exception. Known form: SWIG 4.3.0, whose
# every rescue is `VALUE X_rescue(VALUE args, VALUE error) {`, then
# `  X_call_depth--;`, then `  if (X_call_depth == 0) {`, a brace-balanced body,
# `  }`, `  rb_exc_raise(error);`. That form is rewritten; any other fails, so a
# SWIG which changes this text is read before its output is accepted. The
# rb_exc_raise calls outside the director rescues (SWIG runtime) are left alone.
python3 - "$(swig -version | sed -n 's/^SWIG Version //p')" <<'EOF' || exit 1
import re
import sys

version = sys.argv[1]
sig = re.compile(r'^VALUE (\w+)_rescue\(VALUE args, VALUE error\) \{$')
guard = re.compile(r'^  if \((\w+)_call_depth == 0\) \{$')


def fail(why):
    sys.exit(f"swig.sh: SWIG {version}'s Ruby director rescues: {why} -- read the generated rescues and update this script")


with open('QuickfixRuby.cpp') as f:
    lines = f.read().split('\n')
sig_count = sum(1 for ln in lines if sig.match(ln))
raise_before = sum(ln.count('rb_exc_raise') for ln in lines)

out = []
rewritten = []
i, n = 0, len(lines)
while i < n:
    m = sig.match(lines[i])
    if not m:
        out.append(lines[i]); i += 1; continue
    name = m.group(1)
    out.append(lines[i]); i += 1
    if i >= n or lines[i] != f'  {name}_call_depth--;':
        fail(f'{name}_rescue: expected "  {name}_call_depth--;", found {lines[i]!r}')
    out.append(lines[i]); i += 1
    g = guard.match(lines[i]) if i < n else None
    if not g or g.group(1) != name:
        fail(f'{name}_rescue: expected the depth guard "if ({name}_call_depth == 0) {{", found {lines[i]!r}')
    i += 1
    depth, body = 1, []
    while i < n and depth > 0:
        depth += lines[i].count('{') - lines[i].count('}')
        if depth == 0:
            if lines[i] != '  }':
                fail(f'{name}_rescue: the depth guard closes with {lines[i]!r}, not "  }}"')
            i += 1
            break
        body.append(lines[i]); i += 1
    else:
        fail(f'{name}_rescue: the depth guard is never closed')
    if i >= n or lines[i] != '  rb_exc_raise(error);':
        fail(f'{name}_rescue: expected "  rb_exc_raise(error);" after the guard, found {lines[i]!r}')
    i += 1
    out.extend(body)
    rewritten.append(name)

if not rewritten:
    fail('no director rescue functions found')
if len(rewritten) != sig_count:
    fail(f'rewrote {len(rewritten)} rescues but the file has {sig_count} rescue functions')
text = '\n'.join(out)
raise_after = text.count('rb_exc_raise')
if raise_after != raise_before - len(rewritten):
    fail(f'rb_exc_raise went {raise_before} -> {raise_after}, expected to drop exactly {len(rewritten)}')
if re.search(r'_call_depth == 0', text):
    fail('a "_call_depth == 0" guard still remains after the rewrite')
if text.count('{') != text.count('}'):
    fail(f'braces unbalanced after the rewrite: {text.count("{")} {{ vs {text.count("}")} }}')
with open('QuickfixRuby.cpp', 'w') as f:
    f.write(text)
print(f"swig.sh: ran {len(rewritten)} Ruby director rescues at every call depth and dropped {len(rewritten)} rb_exc_raise ({raise_after} SWIG-runtime calls kept)")
EOF
