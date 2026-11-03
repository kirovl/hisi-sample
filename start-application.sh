#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PATH="$HERE/wifi-runtime/bin:$PATH"
export WPA_CLI="$HERE/wifi-runtime/bin/wpa_cli"
export LD_LIBRARY_PATH="$HERE/wifi-runtime/lib:$HERE/lib"
export CAMERA_AI_HOME="$HERE"
unset CAMERA_AI_SETUP_WIFI
mkdir -p "$HERE/service-runtime"
if [ "${CAMERA_AI_SKIP_WIFI:-0}" != 1 ]; then
    # Driver initialization, saved-profile reconnect and DHCP never block preview.
    nohup sh "$HERE/wifi-provision/maintain-wifi.sh" >"$HERE/service-runtime/wifi-worker.log" 2>&1 </dev/null &
fi
exec sh "$HERE/start-preview.sh" "$@"
