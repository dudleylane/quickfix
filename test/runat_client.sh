#!/bin/sh
# Runs the client definitions -- the harness as the server, at as an initiator
# connecting to it -- one fresh initiator per definition (#146).
#
#   ./runat_client.sh PORT [-t]     (from test/; -t for the threaded initiator)
#
# Like runat.sh, it fails if at does not exit 0 on SIGTERM afterwards.
PORT=$1
shift
if [ -z "$PORT" ]; then
    echo "usage: $0 PORT [at arguments...]"
    exit 2
fi

cat > cfg/at_client.cfg << CFG
[DEFAULT]
ConnectionType=initiator
SocketConnectHost=127.0.0.1
SocketConnectPort=$PORT
ReconnectInterval=1
HeartBtInt=30
StartTime=00:00:00
EndTime=00:00:00
UseDataDictionary=Y
DataDictionary=../spec/FIX42.xml

[SESSION]
BeginString=FIX.4.2
SenderCompID=TW
TargetCompID=ISLD
CFG

total=0
failed=0
for definition in definitions/client/*.def; do
    total=$((total + 1))
    rm -f store/FIX.4.2-TW-ISLD.*
    # The harness listens first: an initiator started earlier would try, fail and
    # advance its sequence numbers before the definition begins.
    ruby -I. Runner.rb 127.0.0.1 "$PORT" "$definition" &
    runner=$!
    waited=0
    until ss -ltn "sport = :$PORT" | grep -q LISTEN || [ "$waited" -ge 50 ]; do
        sleep 0.2
        waited=$((waited + 1))
    done
    ./at -f cfg/at_client.cfg -i "$@" &
    pid=$!
    wait "$runner" || failed=$((failed + 1))
    kill "$pid" 2> /dev/null
    status=0
    wait "$pid" || status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAILED: at -i exited with status $status at shutdown"
        failed=$((failed + 1))
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "FAILED $failed out of $total client definitions"
    exit 1
fi
echo "$total client definitions passed"
