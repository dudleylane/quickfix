#!/bin/sh

swig -I../C++ -threads -D__cpp_noexcept_function_type -DHAVE_SSL=1 -DHAVE_MYSQL -DHAVE_POSTGRESQL -python -c++ -o QuickfixPython.cpp quickfix.i
cat quickfix_fields.py >> quickfix.py
sed -i "s/object.__getattr__/object.__getattribute__/g" quickfix.py

# SWIG 4.3.0's python/director_py_mutex.swg swaps two calls: Swig::Mutex::unlock()
# frees the lock and its destructor merely releases it. The mutex guards a
# director's ownership map, taken whenever a Python LogFactory's create returns a
# Log, so the second such Log locked and freed freed memory (#109). Swap them
# back. Fail rather than skip if the text is not there: a SWIG that fixes this
# should retire the patch, not leave it silently doing nothing.
broken='       ~Mutex() { PyThread_release_lock(mutex_); }
       void lock() { PyThread_acquire_lock(mutex_, WAIT_LOCK); }
       void unlock() { PyThread_free_lock(mutex_); }'
python3 - "$broken" <<'EOF' || exit 1
import sys
broken = sys.argv[1]
fixed = broken.replace('PyThread_release_lock', '@').replace('PyThread_free_lock', 'PyThread_release_lock').replace(
    '@', 'PyThread_free_lock')
with open('QuickfixPython.cpp') as f:
    text = f.read()
if text.count(broken) != 1:
    sys.exit("swig.sh: SWIG's Swig::Mutex is not the one #109 corrects -- check whether this SWIG fixed it")
with open('QuickfixPython.cpp', 'w') as f:
    f.write(text.replace(broken, fixed))
EOF
