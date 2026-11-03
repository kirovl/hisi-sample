#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# sys_config is loaded by the matching SDK startup service.
if ! test -e /dev/dri/card0; then
    test -d /sys/module/ot_board_io_cfg || insmod "$HERE/drivers/ot_board_io_cfg.ko"
    test -d /sys/module/jd9853 || insmod "$HERE/drivers/jd9853.ko"
fi
test -d /sys/module/hynitron_touch || insmod "$HERE/drivers/hynitron_touch.ko"
test -e /dev/dri/card0 || { echo "JD9853 DRM display is unavailable." >&2; exit 1; }
