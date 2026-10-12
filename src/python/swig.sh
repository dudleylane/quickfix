#!/bin/sh

swig -I../C++ -threads -D__cpp_noexcept_function_type -DHAVE_SSL=1 -DHAVE_MYSQL -DHAVE_POSTGRESQL -python -c++ -o QuickfixPython.cpp quickfix.i
cat quickfix_fields.py >> quickfix.py
sed -i "s/object.__getattr__/object.__getattribute__/g" quickfix.py

# SWIG 4.3.0's python/director_py_mutex.swg swaps two calls: Swig::Mutex::unlock()
# frees the lock and its destructor merely releases it. The mutex guards a
# director's ownership map, taken whenever a Python LogFactory's create returns a
# Log, so the second such Log locked and freed freed memory (#109). Swap them
# back. Known to need it: SWIG 4.3.0. A SWIG whose text is already correct is
# accepted as it is (#117); any other text fails, since this guards a double free
# and a guess is not good enough.
broken='       ~Mutex() { PyThread_release_lock(mutex_); }
       void lock() { PyThread_acquire_lock(mutex_, WAIT_LOCK); }
       void unlock() { PyThread_free_lock(mutex_); }'
python3 - "$broken" "$(swig -version | sed -n 's/^SWIG Version //p')" <<'EOF' || exit 1
import sys
broken, version = sys.argv[1], sys.argv[2]
fixed = broken.replace('PyThread_release_lock', '@').replace('PyThread_free_lock', 'PyThread_release_lock').replace(
    '@', 'PyThread_free_lock')
with open('QuickfixPython.cpp') as f:
    text = f.read()
if text.count(broken) == 1:
    with open('QuickfixPython.cpp', 'w') as f:
        f.write(text.replace(broken, fixed))
elif text.count(fixed) == 1:
    print(f"swig.sh: SWIG {version}'s Swig::Mutex is already correct; no patch needed (#109, #117)")
else:
    sys.exit(f"swig.sh: SWIG {version}'s Swig::Mutex is neither 4.3.0's broken one nor the corrected one "
             "-- read it against #109 and update this script (#117)")
EOF
