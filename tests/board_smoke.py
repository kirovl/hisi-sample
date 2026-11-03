#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Ubuntu-only integration test; run after installing camera-ai-lcd on the board.

Uses SDK file inputs and the real board NPU, not the live camera or LCD.
The two-person fixture duplicates a crop of the official SDK human test image.
"""
import argparse
import hashlib
import ipaddress
import pathlib
import re
import shlex
import socket
import subprocess
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=pathlib.Path, required=True)
    parser.add_argument('--board-ip', required=True)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('board-evidence'))
    args = parser.parse_args()
    ip = str(ipaddress.IPv4Address(args.board_ip))
    args.output.mkdir(parents=True, exist_ok=True)
    helper = pathlib.Path(__file__).resolve().parents[1] / 'tools/board-console.py'
    data_dir = args.sdk / 'samples/mpp/svp/ai_component/aidetect/data'
    source = (data_dir / 'hvf_image_hor_1920x1080.yuv').read_bytes()
    if len(source) != 1920 * 1080 * 3 // 2:
        raise RuntimeError('Unexpected SDK fixture layout')
    fixture = bytearray()
    for plane_offset, rows in ((0, 1080), (1920 * 1080, 540)):
        for row in range(rows):
            offset = plane_offset + row * 1920 + 384
            fixture.extend(source[offset:offset + 768] * 2)
    dual = args.output / 'two-people-1536x1080.nv21'
    dual.write_bytes(fixture)
    cases = [
        ('human', data_dir / 'hvf_image_hor_1920x1080.yuv', 1920, 1080, 'HUMAN', 1),
        ('pet', data_dir / 'pet_image_hor_640x384.yuv', 640, 384, 'PET', 1),
        ('package', data_dir / 'pkg_image_hor_1920x1080.yuv', 1920, 1080, 'PACKAGE', 1),
        ('two-people', dual, 1536, 1080, 'HUMAN', 2),
    ]

    def console(command, timeout=120):
        result = subprocess.run([sys.executable, str(helper), '--port', args.port,
            '--timeout', str(timeout), command], capture_output=True, check=True)
        return result.stdout

    console('test -x /opt/camera_ai_lcd/camera_ai_lcd && mkdir -p /opt/camera_ai_lcd/evidence')

    def transfer(local, remote, download=False):
        pidfile = '/tmp/camera-ai-test-' + uuid.uuid4().hex + '.pid'
        action = ('cat ' if download else 'cat > ') + shlex.quote(remote)
        console('! netstat -ltn 2>/dev/null | grep -q ":18889 "')
        try:
            console('{ tcpsvd -c 1 ' + ip + ' 18889 sh -c ' + shlex.quote(action) +
                ' >/tmp/camera-ai-transfer.log 2>&1 & echo $! > ' + pidfile +
                '; }; sleep 1; kill -0 $(cat ' + pidfile + ')')
            with socket.create_connection((ip, 18889), timeout=10) as connection:
                connection.settimeout(120)
                with local.open('wb' if download else 'rb') as stream:
                    if download:
                        while block := connection.recv(262144):
                            stream.write(block)
                    else:
                        while block := stream.read(262144):
                            connection.sendall(block)
                        connection.shutdown(socket.SHUT_WR)
                        while connection.recv(1024):
                            pass
        finally:
            console('if test -s ' + pidfile + '; then kill $(cat ' + pidfile +
                ') 2>/dev/null || true; rm -f ' + pidfile + '; fi')
        digest = hashlib.sha256(local.read_bytes()).hexdigest()
        console("printf '%s\\n' " + shlex.quote(digest + '  ' + remote) + ' | sha256sum -c -')

    for name, source_path, width, height, expected_class, minimum in cases:
        remote_image = '/opt/camera_ai_lcd/evidence/input.nv21'
        remote_ppm = '/opt/camera_ai_lcd/evidence/' + name + '.ppm'
        transfer(source_path, remote_image)
        output = console('sh /opt/camera_ai_lcd/run.sh --headless --models all --input ' +
            remote_image + ' --width ' + str(width) + ' --height ' + str(height) +
            ' --frames 2 --snapshot ' + remote_ppm)
        (args.output / (name + '.log')).write_bytes(output)
        decoded = output.decode(errors='replace')
        frames = decoded.split('FRAME ')[1:]
        if len(frames) != 2 or not all(len(re.findall(r'^OBJECT ' + expected_class + r' ',
                frame, re.MULTILINE)) >= minimum for frame in frames):
            raise RuntimeError(name + ': missing expected detections; see saved log')
        if 'status=ok mode=file' not in decoded or 'dropped=0' not in decoded:
            raise RuntimeError(name + ': incomplete processing')
        transfer(args.output / (name + '.ppm'), remote_ppm, download=True)
        print('PASS ' + name + ': ' + expected_class + ' >= ' + str(minimum), flush=True)
    print('File-input NPU and overlay checks passed. Live camera/LCD remain separate checks.')


if __name__ == '__main__':
    main()
