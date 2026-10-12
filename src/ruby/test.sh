#!/bin/sh
# Runs the Ruby binding tests, exactly as CI does: quickfix.so is the module
# CMake builds into lib/ruby/ or make_ruby.sh builds here, and libquickfix comes
# from lib/.
# Build both first. Works from any directory.
cd "$(dirname "$0")" || exit 1
root=$(cd ../.. && pwd)

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# A module CMake built (-DHAVE_RUBY=ON, lib/ruby/) comes before one make_ruby.sh
# built here (#118). Through RUBYLIB, which the tests' child processes inherit.
export RUBYLIB="$root/lib/ruby:$(pwd)${RUBYLIB:+:$RUBYLIB}"
exec ruby test/TestSuite.rb
