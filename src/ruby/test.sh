#!/bin/sh
# Runs the Ruby binding tests, exactly as CI does: quickfix.so is the module
# make_ruby.sh builds in this directory, and libquickfix comes from lib/.
# Build both first. Works from any directory.
cd "$(dirname "$0")" || exit 1
root=$(cd ../.. && pwd)

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec ruby -I . test/TestSuite.rb
