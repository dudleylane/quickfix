#!/bin/bash
# Starts or stops the MySQL server ut's MySQL cases run against: a rootless podman container on the address
# cfg/ut.cfg names, with the schema from src/sql/mysql and a quickfix user without a password. Nothing outlives it:
# its data is in a tmpfs, it answers on the loopback address only, and `start` first removes one a cancelled run left.
#
# The image is pinned by digest so that CI does not change under a commit; moving to a new one is an edit here.
set -euo pipefail

name=quickfix-mysql
image=docker.io/library/mysql:8.4.13@sha256:a81d043930e51745522bd5c430961cf8286f0a23f0a9a2a6e0108f7ea71b0902
# Above what a short holds (#182), and outside the range the kernel hands out for outgoing connections (32768-60999).
address=127.0.0.1:61306
sql=$(cd "$(dirname "$0")/../src/sql/mysql" && pwd)

stop() { podman rm --force --ignore --time 5 "$name" > /dev/null; }

case "${1:-}" in
start)
    stop
    podman run --detach --rm --name "$name" --publish "$address:3306" --tmpfs /var/lib/mysql \
        --env MYSQL_ALLOW_EMPTY_PASSWORD=yes "$image" > /dev/null

    # Over TCP: the image's entrypoint first runs a server for its own setup, on a socket only, and then replaces it.
    for ((i = 0; i < 180; ++i)); do
        podman exec "$name" mysql -h127.0.0.1 -uroot -e 'SELECT 1' > /dev/null 2>&1 && break
        sleep 1
    done
    podman exec "$name" mysql -h127.0.0.1 -uroot -e 'SELECT 1' > /dev/null \
        || { echo "$name did not answer within 180 s:"; podman logs --tail 20 "$name"; stop; exit 1; }

    # user.sql grants to 'quickfix'@'localhost'; through the published port the client is not localhost.
    {
        echo 'CREATE DATABASE quickfix;'
        cat "$sql"/{sessions,messages,messages_log,event_log}_table.sql
        echo
        sed "s/'localhost'/'%'/" "$sql/user.sql"
    } | podman exec --interactive "$name" mysql -h127.0.0.1 -uroot
    ;;
stop)
    stop
    ;;
*)
    echo "usage: $0 start|stop" >&2
    exit 2
    ;;
esac
