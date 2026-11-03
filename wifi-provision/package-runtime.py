#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Package Wi-Fi-only WS73 runtime files from the vendor staging rootfs."""
import argparse
import hashlib
import pathlib
import shutil

MODULES = {
    "ot_board_io_cfg.ko": ("drv/ko/ot_board_io_cfg.ko", "modules/ot_board_io_cfg.ko"),
    "plat_soc.ko": ("root/plat_soc.ko", "modules/plat_soc.ko"),
    "wifi_soc.ko": ("root/wifi_soc.ko", "modules/wifi_soc.ko"),
}
FILES = {
    "root/wpa_supplicant": "bin/wpa_supplicant",
    "root/wpa_cli": "bin/wpa_cli",
    "lib/libsecurec.so": "lib/libsecurec.so",
}


def module_release(path):
    data = path.read_bytes()
    marker = data.find(b"vermagic=")
    if marker < 0:
        raise SystemExit(f"No vermagic found in {path}")
    value = data[marker + len(b"vermagic="):].split(bytes([0]), 1)[0].decode("ascii")
    return value.split()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_rootfs", type=pathlib.Path,
                        help="vendor rootfs staging tree containing WS73 and wpa_supplicant files")
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    src = args.staged_rootfs.resolve(strict=True)
    dhcp_source = pathlib.Path(__file__).with_name("udhcpc.script")
    if not dhcp_source.is_file():
        raise SystemExit(f"Missing bundled DHCP hook: {dhcp_source}")
    firmware = src / "ws73"
    firmware_names = ("btc_cali.bin", "wifi_cali.bin", "wow.bin", "ws73.bin", "ws73_cfg.ini")
    module_sources = {name: src / rel for name, (rel, _) in MODULES.items()}
    for path in [*(src / rel for rel in FILES), *module_sources.values(),
                 *(firmware / name for name in firmware_names)]:
        if not path.is_file():
            raise SystemExit(f"Required staged file is missing: {path}")
    releases = {name: module_release(path) for name, path in module_sources.items()}
    unique = set(releases.values())
    if len(unique) != 1:
        raise SystemExit(f"WS73 modules have different kernel releases: {releases}")
    out = args.output.resolve()
    if out == src or out in src.parents or src in out.parents:
        raise SystemExit("Runtime output must be separate from staged input")
    marker = out / ".camera-ai-ws73-runtime"
    if out.exists() and any(out.iterdir()) and not marker.is_file():
        raise SystemExit(f"Refusing to overwrite an unrecognized directory: {out}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for source_rel, dest_rel in FILES.items():
        target = out / dest_rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / source_rel, target)
    for name, (source_rel, dest_rel) in MODULES.items():
        target = out / dest_rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / source_rel, target)
    config = out / "etc/wireless/wpa_supplicant.conf"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("ctrl_interface=/etc/wireless/wpa_supplicant\nupdate_config=1\n")
    config.chmod(0o600)
    dhcp = out / "usr/share/udhcpc/default.script"
    dhcp.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dhcp_source, dhcp)
    dhcp.chmod(0o755)
    target_firmware = out / "firmware"
    target_firmware.mkdir()
    for name in firmware_names:
        path = firmware / name
        if not path.is_file():
            raise SystemExit(f"Required WS73 firmware is missing: {path}")
        shutil.copy2(path, target_firmware / name)
    (out / "kernel.release").write_text(next(iter(unique)) + "\n")
    marker.write_text("Generated from vendor WS73 staged rootfs; BLE is not packaged.\n")
    entries = []
    for path in sorted(p for p in out.rglob("*") if p.is_file() and p.name != "SHA256SUMS"):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        entries.append(f"{digest}  {path.relative_to(out)}")
    (out / "SHA256SUMS").write_text("\n".join(entries) + "\n")
    print(f"Packaged WS73 runtime for kernel {next(iter(unique))}: {out}")


if __name__ == "__main__":
    main()
