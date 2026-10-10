#!/bin/sh

# Runs at under the concurrent-churn driver, churn.rb (README, "Exercising the
# concurrent paths"; #94). Run it against a ThreadSanitizer or AddressSanitizer
# build of at: the driver judges only that at kept accepting, and at's exit
# status -- non-zero on a sanitizer report -- decides the rest.
#
#   ./runchurn.sh <port> <workers> <seconds> [at arguments: -t, -l]
. ./acceptor.sh

trap 'stop_acceptor' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

killall ut at

PORT=$1
WORKERS=$2
DURATION=$3
shift 3
./setup.sh $PORT

start_acceptor $PORT "$@"

ruby churn.rb 127.0.0.1 $PORT cfg/at.cfg $WORKERS $DURATION

finish $?
