#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
test -S /run/sdk_server/osal_uds_server.sock || {
    echo 'sdk_server is not ready; use the matching SDK rootfs and start its service.' >&2
    exit 1
}
exec "$HERE/camera_ai_lcd" --models-dir "$HERE/models" "$@"
