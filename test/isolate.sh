#!/bin/sh

# Runs a command in its own user, network and PID namespaces, with a loopback of its own and only its own processes
# in sight. CI's jobs share one machine and run side by side, while the tests bind fixed ports -- ut 5000 and up,
# the acceptance suite 54321 -- and runat.sh begins with `killall ut at`. In here each job's ports and killall are
# its own.
#
#   test/isolate.sh <command> [arguments...]
#
# The command runs as the calling user, with no capabilities, and gets the caller's environment. The namespace's
# first process is a shell that waits for it: a process there ignores any signal it has no handler for, which the
# command should not, and the shell reaps what the command leaves. When that shell ends, or unshare is killed, the
# kernel kills everything left in the namespace. Nothing in it reaches the host's network, so a step that needs
# the host's database stays outside.

exec unshare --map-current-user --keep-caps --net --pid --fork --mount-proc --kill-child \
    sh -c 'ip link set lo up || exit 1
setpriv --inh-caps=-all --ambient-caps=-all -- "$@"
exit $?' isolate "$@"
