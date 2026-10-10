#!/bin/sh

# Runs every acceptance definition against at. acceptor.sh starts at, waits for
# it to listen and judges how it stops.
. ./acceptor.sh

# On an interrupted run the status is already a failure; just clean up.
trap 'stop_acceptor' EXIT
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

start_acceptor $PORT "$@"

cd $DIR
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix4*/*.def definitions/server/fix50/*.def definitions/server/fix50sp1/*.def definitions/server/fix50sp2/*.def definitions/server/validate/*.def definitions/server/future/*.def

finish $?
