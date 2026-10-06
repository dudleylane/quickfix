#!/bin/sh

# Stop the acceptor on the way out, and only the acceptor. Signalling the whole
# process group, as this once did, also signalled this script, so it exited 143
# even when every definition passed. Then wait for it: a dying process keeps
# its listening socket until its memory is freed, and the kernel goes on
# accepting connections for it, so a run started straight after this one had
# its first definition connect to this acceptor and be reset (#74).
trap 'kill "$PROCID" 2>/dev/null; wait "$PROCID" 2>/dev/null' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

killall ut at

RUBY="ruby -I."
DIR=`pwd`
PORT=$1
# Anything after the port goes to at: -t for the threaded transport, -l to log
# session events on stdout.
shift
./setup.sh $PORT

./at -f cfg/at.cfg "$@" &
PROCID=$!
cd $DIR
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix4*/*.def definitions/server/fix50/*.def definitions/server/fix50sp1/*.def definitions/server/fix50sp2/*.def definitions/server/validate/*.def definitions/server/future/*.def

RESULT=$?
exit $RESULT