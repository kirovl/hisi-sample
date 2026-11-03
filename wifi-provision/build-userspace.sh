#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build static ARM Wi-Fi tools in a dedicated tree; vendor sources stay untouched.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(realpath "${1:?Usage: build-userspace.sh HI_AIOT_PROJECT OUTPUT}")
OUT=$(realpath -m "${2:?Supply a separate build output directory}")
TC="$PROJECT/sdk/Hi3516CV610/prebuilts/gcc-10.3-arm-musl-x86-linux-26.06.1/arm-v01c02-linux-musleabi-gcc/bin"
export PATH="$TC:$PATH"
test -x "$TC/arm-v01c02-linux-musleabi-gcc"
mkdir -p "$OUT/source/open_source/openssl"
# These upstream makefiles write final executables into their source directories.
for component in openssl/build-scripts-1.1.1w openssl/openssl-1.1.1w libnl wpa_supplicant; do
    if [ ! -d "$OUT/source/open_source/$component" ]; then
        cp -a "$PROJECT/open_source/$component" "$OUT/source/open_source/$component"
    fi
done
cmake -S "$HERE/userspace" -B "$OUT/build" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$TC/arm-v01c02-linux-musleabi-gcc" \
    -DVENDOR_COPY="$OUT/source" -DPARALLEL_JOBS="${JOBS:-4}"
cmake --build "$OUT/build" --target wpa_supplicant_ext -j"${JOBS:-4}"
echo "Static ARM Wi-Fi tools: $OUT/build/open_source/wpa_supplicant/release"
