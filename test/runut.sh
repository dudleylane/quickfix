#!/bin/sh

# Kills stray engines from an earlier interrupted run. Note this also kills
# unrelated processes named ut or at -- runat.sh does the same.
killall at ut

# Resolve this script's own directory rather than trusting the caller's. The
# previous version took $(pwd) at startup and built the --quickfix-config-file
# and --quickfix-spec-path arguments from it, so both were wrong unless the
# script happened to be invoked from test/.
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$DIR" || exit 1

# ./ut, not ../src/C++/test/ut: the build writes binaries to lib/ and symlinks
# test/{ut,at,pt} to them, so nothing has landed in the source tree since.
# This is how CI invokes the suite.
#
# There is deliberately no `trap ... kill -- -$$` here, unlike runat.sh. That
# script backgrounds `at` and needs to reap it; this one runs ut in the
# foreground and backgrounds nothing, so the trap's only effect was to SIGTERM
# the script and replace ut's exit status -- the actual verdict -- with 143.
./ut --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec "$@"
