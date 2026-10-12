#!/bin/bash
# Runs the example applications as a user would and checks they trade (#114):
# each acceptor example -- the C++ executor plain and over TLS on both
# transports, the Python and Ruby executors, ordermatch -- with tradeclient
# sending it one limit order per FIX version it serves. Every order must draw
# an ExecutionReport, and the acceptor must still be running at the end.
#
#   test/runexamples.sh [CASE...]     (default: every case; run from anywhere)
#
# Needs a build with the examples, -DHAVE_SSL=ON, -DHAVE_PYTHON3=ON for the
# Python case and src/ruby/make_ruby.sh for the Ruby one. Each case runs in a
# fresh copy of bin/cfg, so no store survives from an earlier run.
root=$(cd "$(dirname "$0")/.." && pwd)
lib=$root/lib
cases=${*:-executor executor-ssl executor-ssl-st executor-python executor-ruby ordermatch}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

failed=0
for case in $cases; do
    run="$work/$case/bin"
    mkdir -p "$run"
    cp -r "$root/bin/cfg" "$run/cfg"
    ln -s "$root/spec" "$work/$case/spec"

    port=5001 target=EXECUTOR versions=123456 clientssl=
    case $case in
        executor)        acceptor=("$lib/executor" cfg/executor.cfg) ;;
        executor-ssl)    acceptor=("$lib/executor" cfg/executor.cfg SSL); clientssl=SSL ;;
        executor-ssl-st) acceptor=("$lib/executor" cfg/executor.cfg SSL-ST); clientssl=SSL-ST ;;
        executor-python) acceptor=(env PYTHONPATH="$lib:$root/src/python" python3 "$root/examples/executor/python/executor.py" cfg/executor.cfg) ;;
        executor-ruby)   acceptor=(ruby -I "$root/src/ruby" "$root/examples/executor/ruby/executor.rb" cfg/executor.cfg) ;;
        ordermatch)      acceptor=("$lib/ordermatch" cfg/ordermatch.cfg); port=5002 target=ORDERMATCH versions=3 ;;  # orders are FIX 4.2 only (Application.h)
        *) echo "unknown case: $case"; failed=1; continue ;;
    esac

    # tradeclient's sessions, CLIENT1 to the acceptor for each version served.
    {
        printf '[DEFAULT]\nConnectionType=initiator\nReconnectInterval=2\nFileStorePath=store-client\n'
        printf 'StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n'
        printf 'SocketConnectHost=127.0.0.1\nSocketConnectPort=%s\nSenderCompID=CLIENT1\nTargetCompID=%s\n' "$port" "$target"
        printf 'CertificationAuthoritiesFile=./cfg/certs/certs/cacert.pem\n'
        printf 'ClientCertificateFile=./cfg/certs/127_0_0_1_client.crt\nClientCertificateKeyFile=./cfg/certs/127_0_0_1_client.key\n'
        for v in $(echo "$versions" | grep -o .); do
            case $v in
                1) printf '[SESSION]\nBeginString=FIX.4.0\n' ;; 2) printf '[SESSION]\nBeginString=FIX.4.1\n' ;;
                3) printf '[SESSION]\nBeginString=FIX.4.2\n' ;; 4) printf '[SESSION]\nBeginString=FIX.4.3\n' ;;
                5) printf '[SESSION]\nBeginString=FIX.4.4\n' ;;
                6) printf '[SESSION]\nBeginString=FIXT.1.1\nDefaultApplVerID=FIX.5.0\n' ;;
            esac
        done
    } > "$run/cfg/client.cfg"

    if ss -ltn "sport = :$port" | grep -q LISTEN; then
        echo "[$case] FAILED: something already listens on port $port"
        failed=1
        continue
    fi
    (cd "$run" && exec "${acceptor[@]}") < <(sleep 600) > "$run/acceptor.log" 2>&1 &
    pid=$!
    for _ in $(seq 1 60); do
        ss -ltn "sport = :$port" | grep -q LISTEN && break
        sleep 0.5
    done

    python3 "$root/test/examples_driver.py" "$lib/tradeclient" "$run/cfg/client.cfg" "$target" "$versions" \
        $clientssl | sed "s/^/[$case] /"
    status=${PIPESTATUS[0]}
    if ! kill -0 "$pid" 2> /dev/null; then
        echo "[$case] FAILED: the acceptor exited"
        tail -5 "$run/acceptor.log"
        status=1
    fi
    pkill -P "$pid" 2> /dev/null
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
    [ "$status" -eq 0 ] && echo "[$case] passed" || { echo "[$case] FAILED"; failed=1; }
done
exit $failed
