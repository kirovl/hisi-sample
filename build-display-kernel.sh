#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
PANEL=$(realpath "${1:?Usage: build-display-kernel.sh PANEL_PROJECT OUTPUT}")
OUT=$(realpath -m "${2:?Supply a separate output directory}")
SDK="$PANEL/sdk/Hi3516CV610"
KERNEL="$SDK/third_party/linux/linux-5.10.y"
TOOLS="$SDK/third_party/u-boot/u-boot-2022.07/tools"
TC="$SDK/prebuilts/gcc-10.3-arm-musl-x86-linux-26.06.1/arm-v01c02-linux-musleabi-gcc/bin/arm-v01c02-linux-musleabi-"
FIT="$PANEL/_build/install/image/panel/uImage"
test -f "$FIT" && test -f "$KERNEL/.config" && test -x "${TC}gcc"
if test "$OUT" = "$(dirname "$FIT")"; then echo 'Use a separate output directory.' >&2; exit 2; fi
for option in DRM DRM_MIPI_DBI DRM_KMS_CMA_HELPER SPI BACKLIGHT_PWM; do
    grep -qx "CONFIG_$option=y" "$KERNEL/.config" || { echo "Missing CONFIG_$option=y" >&2; exit 1; }
done
# This base system and its MPP modules use Linux 5.10.221+.
"$KERNEL/scripts/config" --file "$KERNEL/.config" --set-str LOCALVERSION +
make -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" olddefconfig
make -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" -j"$(nproc)" zImage dtbs
RELEASE=$(sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "$KERNEL/include/generated/utsrelease.h")
test "$RELEASE" = '5.10.221+' || { echo "Unexpected kernel release $RELEASE" >&2; exit 1; }
mkdir -p "$OUT/drivers"
for file in board_io_cfg/ot_board_io_cfg.ko lcd/jd9853/jd9853.ko touch/hynitron/hynitron_touch.ko; do
    module="$PANEL/_build/drivers/$file"
    test "$(modinfo -F vermagic "$module" | awk '{print $1}')" = "$RELEASE" || {
        echo "Module/kernel mismatch: $module; rebuild the panel drivers." >&2; exit 1;
    }
    cp "$module" "$OUT/drivers/"
done
# Preserve the panel FIT's injected LCD, touch and board-I/O nodes.
"$TOOLS/dumpimage" -T flat_dt -p 0 -o "$OUT/devicetree.dtb" "$FIT"
cp "$KERNEL/arch/arm/boot/zImage" "$OUT/zImage"
cp "$SDK/boards/dmeb/tools/pc/image_tree_source/linux_image.its" "$OUT/linux_image.its"
(cd "$OUT" && "$TOOLS/mkimage" -f linux_image.its uImage)
printf '%s\n' "$RELEASE" > "$OUT/kernel-release.txt"
(cd "$OUT" && sha256sum uImage drivers/*.ko > SHA256SUMS)
echo "Built display kernel: $OUT/uImage; release=$RELEASE"
echo 'This script builds files only; it does not flash or reboot the board.'
