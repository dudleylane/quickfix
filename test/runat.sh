#!/bin/sh

# Stop the acceptor on the way out, and only the acceptor. Signalling the whole
# process group, as this once did, also signalled this script, so it exited 143
# even when every definition passed.
trap 'kill "$PROCID" 2>/dev/null' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

killall ut at

RUBY="ruby -I."
DIR=`pwd`
PORT=$1
./setup.sh $PORT

./at -f cfg/at.cfg &
PROCID=$!
cd $DIR
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix4*/*.def definitions/server/fix50/*.def definitions/server/fix50sp1/*.def definitions/server/fix50sp2/*.def definitions/server/validate/*.def definitions/server/future/*.def

RESULT=$?
exit $RESULT