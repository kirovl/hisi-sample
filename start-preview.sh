#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sh "$HERE/start-display.sh"
exec sh "$HERE/run.sh" --models all --rotation 90 --screenshot-dir "$HERE/screenshots" "$@"
