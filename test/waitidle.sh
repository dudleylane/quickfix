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
#
# IDLE_WAIVE names repositories, owner/name and space-separated, whose CI does not hold a run: their runners' jobs
# don't count, and the CPU time their runners used in each minute is left out of its busy cores. Each runner here is
# a systemd service, whose cgroup holds its listener, its jobs and whatever they start, so that time is the growth of
# the cgroup's usage_usec, found through its listener's and worker's /proc entries. Work a job moves out of the
# cgroup still counts, and so does the repository's work outside its runners, a benchmark or a build. A cgroup read
# at one end of the minute only, or whose count went backwards, leaves nothing out. Unset, nothing is waived.

IDLE_CPUS=${IDLE_CPUS:-1.0}
IDLE_MINUTES=${IDLE_MINUTES:-3}
IDLE_WAIVE=${IDLE_WAIVE:-}
SERVER=${GITHUB_SERVER_URL:-https://github.com}
# Every runner of this repository has this URL in its .runner file; their jobs don't count.
REPOSITORY=$SERVER/${GITHUB_REPOSITORY:-dudleylane/quickfix}
CPUS=$(grep -c '^cpu[0-9]' /proc/stat)
HZ=$(getconf CLK_TCK)

# Whether IDLE_WAIVE names the repository at the URL given.
waived()
{
    for name in $IDLE_WAIVE; do
        [ "$1" = "$SERVER/$name" ] && return 0
    done
    return 1
}

# The other repositories whose runners are running a job, but those IDLE_WAIVE names. A runner runs each job in a
# Runner.Worker, and the .runner file two directories above it names the repository the runner serves. A job counts
# however little it does, so a repository that gated its runs the same way would wait on this one while this one
# waited on it.
other_repositories()
{
    for pid in $(pgrep -x Runner.Worker); do
        exe=$(readlink "/proc/$pid/exe") || continue
        root=${exe%/*/*}
        url=$(tr -d '\r' < "$root/.runner" 2>/dev/null | sed -n 's/.*"gitHubUrl": *"\([^"]*\)".*/\1/p')
        [ "$url" = "$REPOSITORY" ] || waived "$url" || echo "${url:-$root}"
    done | sort -u | paste -sd ' ' -
}

# The cgroups of the runners IDLE_WAIVE names, one a line: the unified hierarchy's entry in /proc for each one's
# Runner.Listener and, while it runs a job, its Runner.Worker.
waived_cgroups()
{
    [ -n "$IDLE_WAIVE" ] || return 0
    for pid in $(pgrep -x 'Runner\.(Listener|Worker)'); do
        exe=$(readlink "/proc/$pid/exe") || continue
        url=$(tr -d '\r' < "${exe%/*/*}/.runner" 2>/dev/null | sed -n 's/.*"gitHubUrl": *"\([^"]*\)".*/\1/p')
        waived "$url" && sed -n 's/^0:://p' "/proc/$pid/cgroup" 2>/dev/null
    done | sort -u
}

# The CPU time each cgroup given has used so far, one "cgroup microseconds" line each, and nothing for a cgroup
# without a cpu.stat.
usage()
{
    for cgroup in "$@"; do
        awk -v c="$cgroup" '$1 == "usage_usec" { print c, $2 }' "/sys/fs/cgroup$cgroup/cpu.stat" 2>/dev/null
    done
}

# One minute's reading, "busy waived": the cores busy outside the waived runners, and the cores those used. The
# arguments are the minute's ticks, busy and idle together, and its idle ticks; the input is usage's lines at the
# minute's start, a "--" line, and usage's lines at its end. The elapsed time comes from the ticks, which every CPU
# counted, not from sleep's 60 s.
subtract()
{
    awk -v t="$1" -v i="$2" -v n="$CPUS" -v hz="$HZ" '
        $0 == "--" { end = 1; next }
        NF == 2 && $2 ~ /^[0-9]+$/ {
            if (!end) start[$1] = $2
            else if (($1 in start) && $2 + 0 >= start[$1] + 0) used += $2 - start[$1]
        }
        END {
            busy = (t > 0 ? (t - i) / t * n : 0)
            waived = (t > 0 && hz > 0 ? used / 1e6 / (t / n / hz) : 0)
            printf "%.2f %.2f\n", (busy > waived ? busy - waived : 0), waived
        }'
}

# The cores busy over the next minute, all but idle and iowait in /proc/stat's first line, less the waived runners'
# share; and that share.
busy_cpus()
{
    cgroups=$(waived_cgroups)
    start=$(usage $cgroups)
    set -- $(head -1 /proc/stat)
    total=$(($2 + $3 + $4 + $5 + $6 + $7 + $8 + $9)) idle=$(($5 + $6))
    sleep 60
    end=$(usage $cgroups)
    set -- $(head -1 /proc/stat)
    printf '%s\n--\n%s\n' "$start" "$end" |
        subtract $(($2 + $3 + $4 + $5 + $6 + $7 + $8 + $9 - total)) $(($5 + $6 - idle))
}

quiet=0
while [ "$quiet" -lt "$IDLE_MINUTES" ]; do
    set -- $(busy_cpus)
    busy=${1:-} share=${2:-}
    others=$(other_repositories)
    note=${IDLE_WAIVE:+, and $share in the runners of $IDLE_WAIVE, waived}
    # A reading that is not a number counts as busy.
    if [ -z "$others" ] && awk -v b="$busy" -v l="$IDLE_CPUS" 'BEGIN { exit !(b ~ /^[0-9.]+$/ && b < l) }'; then
        quiet=$((quiet + 1))
        echo "$(date -u +%H:%M:%S) quiet: $busy of $CPUS cores busy$note ($quiet of $IDLE_MINUTES min)"
    else
        quiet=0
        echo "$(date -u +%H:%M:%S) waiting: $busy of $CPUS cores busy (limit $IDLE_CPUS)$note${others:+; running a job for $others}"
    fi
done
echo "$(date -u +%H:%M:%S) idle: under $IDLE_CPUS cores busy for $IDLE_MINUTES min, and no other repository's job${IDLE_WAIVE:+ but $IDLE_WAIVE's, waived}"
