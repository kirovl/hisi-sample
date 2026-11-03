#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STATE=/run/camera-ai-lcd
PIDFILE="$STATE/application.pid"
LOG=/tmp/camera-ai-lcd.log
running() {
    pid=$(cat "$PIDFILE" 2>/dev/null || true)
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    kill -0 "$pid" 2>/dev/null || return 1
    command=$(tr '\000' ' ' <"/proc/$pid/cmdline" 2>/dev/null || true)
    case "$command" in *"$HERE/camera-service.sh"*|*"$HERE/camera_ai_lcd"*) return 0;; *) return 1;; esac
}
case "${1:-start}" in
    start)
        test "$(id -u)" = 0 || { echo 'Run as root.' >&2; exit 1; }
        mkdir -p "$STATE"
        if running; then echo "camera-ai-lcd is running (PID $pid)."; exit 0; fi
        test -x "$HERE/camera_ai_lcd"
        if test -f "$HERE/wifi-runtime/kernel.release"; then
            expected=$(cat "$HERE/wifi-runtime/kernel.release")
            test "$(uname -r)" = "$expected" || {
                echo "Kernel mismatch: need $expected, running $(uname -r)." >&2; exit 1;
            }
        fi
        if pidof camera_ai_lcd >/dev/null 2>&1; then
            echo 'Another camera_ai_lcd instance is running; stop it first.' >&2; exit 1
        fi
        mkdir "$STATE/starting" 2>/dev/null || { echo 'Service is already starting.'; exit 1; }
        trap 'rmdir "$STATE/starting" 2>/dev/null || true' EXIT
        # Keep capture logs in RAM instead of continuously writing NAND.
        nohup sh "$HERE/camera-service.sh" run >"$LOG" 2>&1 </dev/null &
        echo $! >"$PIDFILE"
        echo "Starting camera-ai-lcd; log: $LOG"
        ;;
    run)
        attempts=0
        while ! test -S /run/sdk_server/osal_uds_server.sock; do
            attempts=$((attempts + 1))
            test "$attempts" -le 60 || { echo 'sdk_server startup timed out.' >&2; exit 1; }
            sleep 1
        done
        fifo="$STATE/output.$$"
        mkfifo "$fifo"
        child=''
        logger=''
        cleanup() {
            trap - EXIT INT TERM
            if test -n "$child"; then kill "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
            if test -n "$logger"; then kill "$logger" 2>/dev/null || true; fi
            rm -f "$fifo"
        }
        trap cleanup EXIT
        trap 'exit 0' INT TERM
        # Limit frame logs in RAM. awk is supplied by the board BusyBox.
        awk -v path="$LOG" '{
            if (NR % 2000 == 1) { close(path); printf "" > path }
            print >> path
            fflush(path)
        }' <"$fifo" &
        logger=$!
        sh "$HERE/start-application.sh" >"$fifo" 2>&1 &
        child=$!
        wait "$child"
        ;;
    stop)
        if running; then
            kill "$pid"
            attempts=0
            while running && test "$attempts" -lt 15; do sleep 1; attempts=$((attempts + 1)); done
            if running; then echo 'Application did not stop; inspect the log.' >&2; exit 1; fi
        fi
        rm -f "$PIDFILE"
        echo 'camera-ai-lcd stopped.'
        ;;
    restart) sh "$0" stop; exec sh "$0" start;;
    status)
        if running; then echo "camera-ai-lcd is running (PID $pid)."; else echo 'camera-ai-lcd is stopped.'; exit 1; fi
        ;;
    *) echo "Usage: $0 {start|stop|restart|status}" >&2; exit 2;;
esac
