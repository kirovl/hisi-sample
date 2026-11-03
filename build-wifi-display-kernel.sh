#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build only: keep the working panel tree and board untouched.
set -euo pipefail
PANEL=$(realpath "${1:?Usage: build-wifi-display-kernel.sh PANEL_PROJECT SDK OUTPUT}")
SDK=$(realpath "${2:?Supply the complete Hi3516CV610 SDK directory}")
OUT=$(realpath -m "${3:?Supply a new output directory}")
SOURCE="$PANEL/sdk/Hi3516CV610/third_party/linux/linux-5.10.y"
KERNEL="$OUT/linux"
TC="$SDK/prebuilts/gcc-10.3-arm-musl-x86-linux-26.06.1/arm-v01c02-linux-musleabi-gcc/bin/arm-v01c02-linux-musleabi-"
TOOLS="$PANEL/sdk/Hi3516CV610/third_party/u-boot/u-boot-2022.07/tools"
test -f "$SOURCE/.config" && test -x "${TC}gcc"
if [ -e "$OUT" ]; then
    if [ "${RESUME:-0}" != 1 ] || [ ! -f "$OUT/.camera-ai-wifi-kernel" ]; then
        echo 'Use a new output directory, or RESUME=1 for this script’s marked build.' >&2
        exit 2
    fi
else
    mkdir -p "$OUT"
    touch "$OUT/.camera-ai-wifi-kernel"
    cp -R --reflink=auto "$SOURCE" "$KERNEL"
fi
# Official ws73_lcd/patch/apply_patches.sh uses an empty .scmversion.
# The existing camera panel build additionally set LOCALVERSION="+"; clear it.
: > "$KERNEL/.scmversion"
"$KERNEL/scripts/config" --file "$KERNEL/.config" --set-str LOCALVERSION '' \
    --disable LOCALVERSION_AUTO --enable WIRELESS --enable CFG80211 --enable MAC80211
make -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" olddefconfig
# Refresh copied generated headers/auto.conf before asking for kernelrelease.
make -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" prepare
RELEASE=$(make -s -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" kernelrelease)
test "$RELEASE" = 5.10.221
make -C "$KERNEL" ARCH=arm CROSS_COMPILE="$TC" -j"${JOBS:-4}" zImage modules
mkdir -p "$OUT/drivers"
for part in board_io_cfg lcd/jd9853 touch/hynitron; do
    dest="$OUT/adapter/$part"
    mkdir -p "$dest"
    cp -R "$PANEL/_build/drivers/$part/." "$dest/"
    make -C "$KERNEL" M="$dest" ARCH=arm CROSS_COMPILE="$TC" clean
    make -C "$KERNEL" M="$dest" ARCH=arm CROSS_COMPILE="$TC" -j"${JOBS:-4}" modules
    cp "$dest/"*.ko "$OUT/drivers/"
done
# Rebuild MPP wrappers too: existing camera modules target 5.10.221+.
# SDK makefiles honor command-line overrides for isolated temp/release paths.
mkdir -p "$OUT/mpp/ko"
cp -R "$SDK/output/mpp/ko/." "$OUT/mpp/ko/"
export PATH="$(dirname "$TC"):$PATH"
make -C "$SDK/soc/drivers" -j"${JOBS:-4}" KERNEL_ROOT="$KERNEL" \
    TEMP_PATH="$OUT/mpp-temp" REL_DIR="$OUT/mpp" REL_KO="$OUT/mpp/ko"
for module in "$OUT/drivers/"*.ko "$OUT/mpp/ko/"*.ko; do
    test "$(modinfo -F vermagic "$module" | awk '{print $1}')" = "$RELEASE" || {
        echo "Unrebuilt module: $module" >&2; exit 1;
    }
done
# Retain proven JD9853/touch/sensor DT nodes, not a generic base DTB.
"$TOOLS/dumpimage" -T flat_dt -p 0 -o "$OUT/devicetree.dtb" "$PANEL/_build/install/image/panel/uImage"
cp "$KERNEL/arch/arm/boot/zImage" "$OUT/zImage"
cp "$SDK/boards/dmeb/tools/pc/image_tree_source/linux_image.its" "$OUT/linux_image.its"
(cd "$OUT" && "$TOOLS/mkimage" -f linux_image.its uImage)
printf '%s\n' "$RELEASE" > "$OUT/kernel-release.txt"
(cd "$OUT" && sha256sum uImage drivers/*.ko mpp/ko/*.ko > SHA256SUMS)
echo "Built $OUT/uImage and matching drivers; no flash write or reboot performed."
