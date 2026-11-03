#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INIT=/etc/init.d/S99camera-ai-lcd
test "$(id -u)" = 0 || { echo 'Run on the board as root.' >&2; exit 1; }
test -d /etc/init.d || { echo '/etc/init.d is missing.' >&2; exit 1; }
case "${1:-enable}" in
    enable)
        case "$HERE" in *[!A-Za-z0-9_./-]*) echo 'Use an application path without spaces or shell characters.' >&2; exit 1;; esac
        test -f "$HERE/camera-service.sh" && test -x "$HERE/camera_ai_lcd"
        if test -e "$INIT" && ! grep -q '# Managed by CV610 camera-ai-lcd' "$INIT"; then
            echo "Refusing to replace an unrelated init script: $INIT" >&2; exit 1
        fi
        temp="$INIT.new.$$"
        trap 'rm -f "$temp"' EXIT
        {
            echo '#!/bin/sh'
            echo '# Managed by CV610 camera-ai-lcd'
            printf 'exec /bin/sh %s/camera-service.sh "${1:-start}"\n' "$HERE"
        } >"$temp"
        chmod 755 "$temp"
        mv "$temp" "$INIT"
        sync
        echo "Enabled: $INIT -> $HERE"
        echo "Start now: sh $HERE/camera-service.sh start"
        ;;
    disable)
        if test -f "$INIT"; then
            grep -q '# Managed by CV610 camera-ai-lcd' "$INIT" || { echo 'Not a managed init entry.' >&2; exit 1; }
            rm "$INIT"
            sync
        fi
        echo 'Boot autostart disabled; the running application is unchanged.'
        ;;
    *) echo "Usage: $0 {enable|disable}" >&2; exit 2;;
esac
