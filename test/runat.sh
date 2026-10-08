#!/bin/sh

# Stop the acceptor and wait for it to exit, giving it STOP_TIMEOUT seconds
# before SIGKILL. Signal only the acceptor: signalling the whole process group,
# as this once did, also signalled this script, so it exited 143 even when
# every definition passed. And wait: a dying process keeps its listening socket
# until its memory is freed, and the kernel goes on accepting connections for
# it, so a run started straight after this one had its first definition connect
# to this acceptor and be reset (#74). Returns at's exit status, or 137 if it
# had to be killed.
STOP_TIMEOUT=30
stop_acceptor()
{
    [ -n "$PROCID" ] || return 0
    pid=$PROCID
    PROCID=
    kill "$pid" 2>/dev/null
    waited=0
    while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt "$STOP_TIMEOUT" ]; do
        sleep 1
        waited=$((waited + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        echo "at did not stop within ${STOP_TIMEOUT}s of SIGTERM; killing it"
        kill -9 "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        return 137
    fi
    wait "$pid"
}

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

./at -f cfg/at.cfg "$@" &
PROCID=$!
cd $DIR
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix4*/*.def definitions/server/fix50/*.def definitions/server/fix50sp1/*.def definitions/server/fix50sp2/*.def definitions/server/validate/*.def definitions/server/future/*.def

RESULT=$?

# at shuts down cleanly on SIGTERM, so a non-zero status is a failure of the
# run even when every definition passed: a crash or hang in Acceptor::stop()
# and session teardown, or, under AddressSanitizer, a leak report (#84).
stop_acceptor
ATRESULT=$?
if [ "$ATRESULT" -ne 0 ]; then
    echo "FAILED: at exited with status $ATRESULT at shutdown"
    [ "$RESULT" -ne 0 ] || RESULT=$ATRESULT
fi
exit $RESULT
