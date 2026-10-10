#!/bin/sh
# Needs the Ruby module, built by src/ruby/make_ruby.sh into src/ruby beside
# quickfix_ruby.rb, and libquickfix from lib/, as in src/ruby/test.sh.
cd "$(dirname "$0")" || exit 1
root=$(cd .. && pwd)

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec ruby -I "$root/src/ruby" ../examples/executor/ruby/executor.rb cfg/executor.cfg
