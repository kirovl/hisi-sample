#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Initialize only WS73 SDIO Wi-Fi; Bluetooth and BLE provisioning are not used.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$(CDPATH= cd -- "$HERE/.." && pwd)
WIFI="$APP/wifi-runtime"

if [ "$(id -u)" != 0 ]; then
    echo "Run this setup as root." >&2
    exit 1
fi
test -f "$WIFI/kernel.release" || {
    echo "Missing packaged WS73 runtime under $WIFI." >&2
    echo "Build the vendor WS73 Wi-Fi runtime and package it first." >&2
    exit 1
}
EXPECTED=$(cat "$WIFI/kernel.release")
ACTUAL=$(uname -r)
if [ "$EXPECTED" != "$ACTUAL" ]; then
    echo "WS73 modules target kernel $EXPECTED; running kernel is $ACTUAL." >&2
    echo "Refusing to load mismatched kernel modules." >&2
    exit 1
fi
for file in "$WIFI/modules/ot_board_io_cfg.ko" "$WIFI/modules/plat_soc.ko" \
            "$WIFI/modules/wifi_soc.ko" \
            "$WIFI/bin/wpa_supplicant" "$WIFI/bin/wpa_cli" \
            "$WIFI/etc/wireless/wpa_supplicant.conf"; do
    test -f "$file" || { echo "Missing required Wi-Fi component: $file" >&2; exit 1; }
done
if [ ! -e /ws73 ]; then
    ln -s "$WIFI/firmware" /ws73
elif [ ! -f /ws73/ws73.bin ]; then
    echo "/ws73 exists but does not contain WS73 firmware; refusing to replace it." >&2
    exit 1
fi
module_loaded() { grep -q "^$1 " /proc/modules; }
load_module() {
    name=$1
    file=$2
    if ! module_loaded "$name"; then
        insmod "$file"
    fi
}
load_module ot_board_io_cfg "$WIFI/modules/ot_board_io_cfg.ko"
load_module plat_soc "$WIFI/modules/plat_soc.ko"
load_module wifi_soc "$WIFI/modules/wifi_soc.ko"
attempt=0
while ! ifconfig wlan0 >/dev/null 2>&1 && [ "$attempt" -lt 20 ]; do
    attempt=$((attempt + 1))
    sleep 1
done
ifconfig wlan0 >/dev/null 2>&1 || { echo "WS73 wlan0 interface did not appear." >&2; exit 1; }
ifconfig wlan0 up
mkdir -p /etc/wireless /var/run /etc/wireless/wpa_supplicant
mkdir -p /usr/share/udhcpc
if [ ! -f /usr/share/udhcpc/default.script ]; then
    cp "$WIFI/usr/share/udhcpc/default.script" /usr/share/udhcpc/default.script
    chmod 755 /usr/share/udhcpc/default.script
fi
if [ ! -f /etc/wireless/wpa_supplicant.conf ]; then
    cp "$WIFI/etc/wireless/wpa_supplicant.conf" /etc/wireless/wpa_supplicant.conf
fi
if ! grep -q '^update_config=1$' /etc/wireless/wpa_supplicant.conf; then
    printf '\nupdate_config=1\n' >> /etc/wireless/wpa_supplicant.conf
fi
chmod 600 /etc/wireless/wpa_supplicant.conf
export PATH="$WIFI/bin:/sbin:/bin:/usr/sbin:/usr/bin"
export LD_LIBRARY_PATH="$WIFI/lib:$WIFI/bin"
if ! pidof wpa_supplicant >/dev/null 2>&1; then
    "$WIFI/bin/wpa_supplicant" -B -i wlan0 -c /etc/wireless/wpa_supplicant.conf \
        -C /etc/wireless/wpa_supplicant
fi
sleep 1
if ! "$WIFI/bin/wpa_cli" -i wlan0 -p /etc/wireless/wpa_supplicant ping 2>/dev/null | grep -q PONG; then
    echo "wpa_supplicant control socket is unavailable." >&2
    exit 1
fi
udhcpc -i wlan0 -q -n -t 3 -s /usr/share/udhcpc/default.script >/dev/null 2>&1 || true
echo "WS73 Wi-Fi ready; configure a network on the touchscreen."
