#!/bin/sh

# Waits until the machine is idle of other projects. CI's self-hosted runners share it with other repositories'
# runners and with their benchmarks: a run started beside them disturbs their work, and theirs disturbs its tests
# and timings.
#
#   test/waitidle.sh
#
# Idle means that no runner serving another repository is running a job, and that fewer than IDLE_CPUS cores
# (default 1.0) were busy in each of IDLE_MINUTES minutes in a row (default 3). The desktop and idle sessions use
# less than one core; a benchmark or a build uses more. Each minute prints its reading, so a run that waits for
# hours shows why in its log, and the quiet minutes show how far under the limit an idle machine sits. The job's
# timeout bounds the wait.

IDLE_CPUS=${IDLE_CPUS:-1.0}
IDLE_MINUTES=${IDLE_MINUTES:-3}
# Every runner of this repository has this URL in its .runner file; their jobs don't count.
REPOSITORY=${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-dudleylane/quickfix}
CPUS=$(grep -c '^cpu[0-9]' /proc/stat)

# The other repositories whose runners are running a job. A runner runs each job in a Runner.Worker, and the
# .runner file two directories above it names the repository the runner serves. A job counts however little it
# does, so a repository that gated its runs the same way would wait on this one while this one waited on it.
other_repositories()
{
    for pid in $(pgrep -x Runner.Worker); do
        exe=$(readlink "/proc/$pid/exe") || continue
        root=${exe%/*/*}
        url=$(tr -d '\r' < "$root/.runner" 2>/dev/null | sed -n 's/.*"gitHubUrl": *"\([^"]*\)".*/\1/p')
        [ "$url" = "$REPOSITORY" ] || echo "${url:-$root}"
    done | sort -u | paste -sd ' ' -
}

# The cores busy over the next minute: all but idle and iowait in /proc/stat's first line.
busy_cpus()
{
    set -- $(head -1 /proc/stat)
    total=$(($2 + $3 + $4 + $5 + $6 + $7 + $8 + $9)) idle=$(($5 + $6))
    sleep 60
    set -- $(head -1 /proc/stat)
    awk -v t=$(($2 + $3 + $4 + $5 + $6 + $7 + $8 + $9 - total)) -v i=$(($5 + $6 - idle)) -v n="$CPUS" \
        'BEGIN { printf "%.2f", (t > 0 ? (t - i) / t * n : 0) }'
}

quiet=0
while [ "$quiet" -lt "$IDLE_MINUTES" ]; do
    busy=$(busy_cpus)
    others=$(other_repositories)
    # A reading that is not a number counts as busy.
    if [ -z "$others" ] && awk -v b="$busy" -v l="$IDLE_CPUS" 'BEGIN { exit !(b ~ /^[0-9.]+$/ && b < l) }'; then
        quiet=$((quiet + 1))
        echo "$(date -u +%H:%M:%S) quiet: $busy of $CPUS cores busy ($quiet of $IDLE_MINUTES min)"
    else
        quiet=0
        echo "$(date -u +%H:%M:%S) waiting: $busy of $CPUS cores busy (limit $IDLE_CPUS)${others:+; running a job for $others}"
    fi
done
echo "$(date -u +%H:%M:%S) idle: under $IDLE_CPUS cores busy for $IDLE_MINUTES min, and no other repository's job"
