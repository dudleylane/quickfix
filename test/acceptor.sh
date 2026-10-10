# Starting and stopping the acceptance acceptor, for runat.sh and runchurn.sh.
# Source it from test/, where it runs ./at.

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

# start_acceptor PORT [at arguments...]: starts at on cfg/at.cfg and returns
# once it listens on PORT, or exits the script if it cannot.
start_acceptor()
{
    PORT=$1
    shift
    ./at -f cfg/at.cfg "$@" &
    PROCID=$!

    # Wait for at to listen before the first client connects. Runner.rb only
    # retries each connect for 29 s, and a sanitizer build of at takes longer than
    # that to start, which failed the first definitions with "Connection refused"
    # (#85). The socket must belong to this at: one still held by an earlier
    # acceptor (#74) is not readiness.
    START_TIMEOUT=300
    waited=0
    until ss -ltnp "sport = :$PORT" 2>/dev/null | grep -q "pid=$PROCID,"; do
        if ! kill -0 "$PROCID" 2>/dev/null; then
            echo "FAILED: at exited before listening on port $PORT"
            exit 1
        fi
        if [ "$waited" -ge "$START_TIMEOUT" ]; then
            echo "FAILED: at did not listen on port $PORT within ${START_TIMEOUT}s"
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

# finish RESULT: stops at and exits with RESULT, or with at's status if RESULT
# is 0 and at's is not.
finish()
{
    RESULT=$1

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
}
