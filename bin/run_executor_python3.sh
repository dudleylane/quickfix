#!/bin/sh
# Needs a build with -DHAVE_PYTHON3=ON: _quickfix.so and libquickfix come from
# lib/, quickfix.py from src/python, as in src/python/test-python3.sh.
cd "$(dirname "$0")" || exit 1
root=$(cd .. && pwd)

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$root/lib:$root/src/python"

exec python3 ../examples/executor/python/executor.py cfg/executor.cfg
