#!/bin/sh

# Compares this tree's pt with the base commit's and fails on a regression
# (#93). Run from test/ after a Release build, so that ./pt is this tree's:
#
#   ./ptgate.sh <base commit> [count]
#
# The base is built in a temporary worktree, Release, into its own output
# directory. The two are then run alternately -- base, head, base, head --
# pinned to two cores, each run starting once the load has settled or after
# ten minutes regardless, and ptcompare.py judges the four runs.

BASE=$1
COUNT=${2:-500000}
if [ -z "$BASE" ]; then
    echo "usage: $0 <base commit> [count]"
    exit 2
fi

if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
    echo "FAILED: $BASE is not a commit"
    exit 2
fi

TEST=$(pwd)
WORK=$(mktemp -d)
trap 'git -C "$TEST" worktree remove --force "$WORK/base" 2>/dev/null; rm -rf "$WORK"' EXIT

git -C "$TEST" worktree add --detach "$WORK/base" "$BASE" >/dev/null || exit 1
cmake -S "$WORK/base" -B "$WORK/base/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=ON \
    -DQUICKFIX_TESTS=OFF -DQUICKFIX_LIB_OUTPUT_DIR="$WORK/base/out" >/dev/null || exit 1
cmake --build "$WORK/base/build" --target pt -j"$(nproc)" >/dev/null || exit 1

settle()
{
    waited=0
    while [ "$(awk '{ print ($1 < 2.0) }' /proc/loadavg)" != 1 ] && [ "$waited" -lt 600 ]; do
        sleep 10
        waited=$((waited + 10))
    done
}

# Each pt runs from its own tree's test/, where its ../spec is.
run()
{
    settle
    echo "$1 run $2, load $(cut -d' ' -f1 /proc/loadavg)"
    (cd "$3" && taskset -c 2,3 "$4" -p "$5" -c "$COUNT") > "$WORK/$1-$2.txt" 2>&1 || {
        cat "$WORK/$1-$2.txt"
        echo "FAILED: the $1 pt exited non-zero"
        exit 1
    }
}

for i in 1 2; do
    run base $i "$WORK/base/test" "$WORK/base/out/pt" 54323
    run head $i "$TEST" "$TEST/pt" 54324
done

python3 "$TEST/ptcompare.py" "$WORK/base-1.txt" "$WORK/head-1.txt" "$WORK/base-2.txt" "$WORK/head-2.txt"
