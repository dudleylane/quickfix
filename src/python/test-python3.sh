#!/bin/sh
# Runs the Python binding tests against the CMake build, exactly as CI does:
# _quickfix.so and libquickfix come from lib/, quickfix.py from this directory.
# Build first with -DHAVE_PYTHON3=ON. Works from any directory.
cd "$(dirname "$0")" || exit 1
root=$(cd ../.. && pwd)

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$root/lib:$root/src/python"

exec python3 -m unittest discover -s test -p '*TestCase.py' -v
