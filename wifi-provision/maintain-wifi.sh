#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Saved profiles only: never joins an unknown open network.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$(CDPATH= cd -- "$HERE/.." && pwd)
RUNTIME="$APP/service-runtime"
mkdir -p "$RUNTIME"
LOCK="$RUNTIME/wifi-worker.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
    old=$(cat "$LOCK/pid" 2>/dev/null || true)
    case "$old" in ''|*[!0-9]*) exit 0;; esac
    if kill -0 "$old" 2>/dev/null; then exit 0; fi
    rm -f "$LOCK/pid"
    rmdir "$LOCK" 2>/dev/null || exit 0
    mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$LOCK/pid"
trap 'rm -f "$LOCK/pid"; rmdir "$LOCK" 2>/dev/null || true' EXIT
trap 'exit 0' INT TERM
export PATH="$APP/wifi-runtime/bin:/sbin:/bin:/usr/sbin:/usr/bin"
export LD_LIBRARY_PATH="$APP/wifi-runtime/lib:$APP/lib"
sh "$HERE/start-ws73.sh"
cli() { "$APP/wifi-runtime/bin/wpa_cli" -i wlan0 -p /etc/wireless/wpa_supplicant "$@"; }
ipv4() { ifconfig "$1" 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p'; }
last_ip=''
while :; do
    ui=$(cat "$RUNTIME/wifi-ui.pid" 2>/dev/null || true)
    active=0
    case "$ui" in ''|*[!0-9]*) ;; *) if kill -0 "$ui" 2>/dev/null; then active=1; fi;; esac
    if [ "$active" -eq 0 ]; then
        state=$(cli status 2>/dev/null | sed -n 's/^wpa_state=//p')
        case "$state" in DISCONNECTED|INACTIVE) cli reconnect >/dev/null 2>&1 || true;; esac
        if [ "$state" = COMPLETED ] && [ -z "$(ipv4 wlan0)" ]; then
            udhcpc -i wlan0 -q -n -t 3 -s /usr/share/udhcpc/default.script >/dev/null 2>&1 || true
        fi
    fi
    address=$(ipv4 wlan0)
    if [ -z "$address" ]; then address=$(ipv4 eth0); fi
    if [ -n "$address" ] && [ "$address" != "$last_ip" ]; then
        if sh "$APP/serve-screenshots.sh" start "$address"; then last_ip=$address; fi
    fi
    sleep 10
done
