#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SHOTS="$HERE/screenshots"
RUNTIME="$HERE/service-runtime"
BBOX="$HERE/service-bin/busybox"
write_index() {
    printf '[' > "$SHOTS/index.json.tmp"
    separator=''
    for path in "$SHOTS"/shot-*.png; do
        test -f "$path" || continue
        name=$(basename "$path")
        case "$name" in *[!a-zA-Z0-9_.-]*) continue;; esac
        printf '%s"%s"' "$separator" "$name" >> "$SHOTS/index.json.tmp"
        separator=,
    done
    printf ']\n' >> "$SHOTS/index.json.tmp"
    mv "$SHOTS/index.json.tmp" "$SHOTS/index.json"
}
stop_services() {
    for name in ftp http index; do
        if test -s "$RUNTIME/$name.pid"; then
            pid=$(cat "$RUNTIME/$name.pid")
            case "$pid" in ''|*[!0-9]*) continue;; esac
            if test -r "/proc/$pid/cmdline" && tr '\000' ' ' < "/proc/$pid/cmdline" | grep -Fq "$HERE/"; then
                kill "$pid" 2>/dev/null || true
            fi
            rm -f "$RUNTIME/$name.pid"
        fi
    done
}
case "${1:-start}" in
    index-loop)
        while :; do write_index; sleep 2; done
        ;;
    stop)
        stop_services
        echo 'Screenshot services stopped.'
        ;;
    start)
        test -x "$BBOX" || { echo 'Missing service-bin/busybox; build and install the optional screenshot services first.' >&2; exit 1; }
        mkdir -p "$SHOTS" "$RUNTIME"
        stop_services
        cp "$HERE/gallery/index.html" "$SHOTS/index.html"
        write_index
        IP="${2:-$(ip -4 addr show eth0 | awk '/inet / {split($2,a,"/");print a[1];exit}')}"
        test -n "$IP" || { echo 'No eth0 IPv4 address.' >&2; exit 1; }
        "$BBOX" tcpsvd -c 4 -E "$IP" 80 "$BBOX" ftpd -t 120 -T 600 "$SHOTS" > "$RUNTIME/ftp.log" 2>&1 &
        echo $! > "$RUNTIME/ftp.pid"
        "$BBOX" httpd -f -p "$IP:8080" -h "$SHOTS" > "$RUNTIME/http.log" 2>&1 &
        echo $! > "$RUNTIME/http.pid"
        sh "$HERE/serve-screenshots.sh" index-loop > "$RUNTIME/index.log" 2>&1 &
        echo $! > "$RUNTIME/index.pid"
        sleep 1
        for name in ftp http index; do kill -0 "$(cat "$RUNTIME/$name.pid")"; done
        echo "Web gallery: http://$IP:8080/"
        echo "Read-only anonymous FTP: ftp://$IP:80/"
        ;;
    *) echo 'Usage: serve-screenshots.sh start [BOARD_IP] | stop' >&2; exit 2;;
esac
