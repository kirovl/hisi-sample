#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="$(realpath "${1:?Usage: $0 SDK_ROOT ARM_LIBDRM_PREFIX [OUTPUT]}")"
DRM="$(realpath "${2:?Supply the ARM libdrm release prefix}")"
OUT="$(realpath -m "${3:-$HERE/output}")"
TC="$SDK/prebuilts/gcc-10.3-arm-musl-x86-linux-26.06.1/arm-v01c02-linux-musleabi-gcc/bin"
CC="$TC/arm-v01c02-linux-musleabi-gcc"
CXX="$TC/arm-v01c02-linux-musleabi-g++"
test -x "$CC" && test -x "$CXX"
test -f "$DRM/lib/libdrm.a"
cmake -S "$HERE" -B "$OUT/build" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DSDK_ROOT="$SDK" -DDRM_PREFIX="$DRM" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT/build" -j"$(nproc)"
PKG="$OUT/package"
mkdir -p "$PKG/lib" "$PKG/models"
cp "$OUT/build/camera_ai_lcd" "$OUT/build/wifi_setup" "$PKG/"
cp "$HERE/run.sh" "$HERE/start-display.sh" "$HERE/start-preview.sh" "$HERE/start-application.sh" "$PKG/"
mkdir -p "$PKG/wifi-provision"
cp "$HERE/wifi-provision/maintain-wifi.sh" "$HERE/wifi-provision/start-ws73.sh" "$HERE/wifi-provision/package-runtime.py" "$HERE/wifi-provision/udhcpc.script" "$PKG/wifi-provision/"
# Optional Wi-Fi-only WS73 runtime; no BLE module or phone app is used.
if test -n "${WIFI_RUNTIME:-}"; then
    test -f "$WIFI_RUNTIME/kernel.release"
    mkdir -p "$PKG/wifi-runtime"
    cp -a "$WIFI_RUNTIME/." "$PKG/wifi-runtime/"
fi
# Optional: modules must come from the exact kernel running on the board.
if test -n "${PANEL_DRIVERS:-}"; then
    mkdir -p "$PKG/drivers"
    cp "$PANEL_DRIVERS/board_io_cfg/ot_board_io_cfg.ko" "$PKG/drivers/"
    cp "$PANEL_DRIVERS/lcd/jd9853/jd9853.ko" "$PKG/drivers/"
    cp "$PANEL_DRIVERS/touch/hynitron/hynitron_touch.ko" "$PKG/drivers/"
fi
cp -a "$SDK/soc/libs/musl/"*.so* "$PKG/lib/"
for model in det_hvf_hor.bin det_pet_hor.bin det_pkg_hor.bin; do
    cp "$SDK/output/mpp/model/aidetect/$model" "$PKG/models/"
done
# Toolchain C++ runtime may not be included in the vendor library directory.
for lib in libstdc++.so.6 libgcc_s.so.1; do
    source_path="$($CC -print-file-name="$lib")"
    if test -f "$source_path"; then cp -L "$source_path" "$PKG/lib/$lib"; fi
done
# Optional separate, read-only screenshot services. Do not replace /bin/busybox.
if test -n "${SERVICE_BUILD:-}"; then
    mkdir -p "$PKG/service-bin" "$PKG/gallery" "$PKG/licenses"
    cp "$SERVICE_BUILD/busybox" "$PKG/service-bin/busybox"
    chmod 755 "$PKG/service-bin/busybox"
    cp "$SERVICE_BUILD/BUSYBOX-LICENSE" "$SERVICE_BUILD/busybox.config" "$PKG/licenses/"
    cp "$HERE/serve-screenshots.sh" "$PKG/"
    cp "$HERE/gallery/index.html" "$PKG/gallery/"
fi
(cd "$PKG" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
tar -czf "$OUT/camera-ai-lcd.tar.gz" -C "$PKG" .
sha256sum "$OUT/camera-ai-lcd.tar.gz"
printf 'Package: %s\n' "$OUT/camera-ai-lcd.tar.gz"
