#!/usr/bin/env python3
"""Install the camera AI LCD package using Ethernet; UART carries Linux commands only."""
import argparse
import hashlib
import ipaddress
import pathlib
import shlex
import socket
import subprocess
import sys
import tarfile
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=pathlib.Path)
    parser.add_argument('--board-ip', required=True)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--no-autostart', action='store_true',
                        help='install files without registering the boot startup service')
    args = parser.parse_args()
    board_ip = str(ipaddress.IPv4Address(args.board_ip))
    archive = args.archive.resolve(strict=True)
    if not args.no_autostart:
        with tarfile.open(archive) as package:
            files = {item.name.removeprefix('./') for item in package if item.isfile()}
        required = {'camera-service.sh', 'enable-autostart.sh'}
        if not required.issubset(files):
            parser.error('archive lacks autostart scripts; rebuild it or use --no-autostart')
    helper = pathlib.Path(__file__).parent / 'tools' / 'board-console.py'
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    pidfile = '/tmp/cv610-ai-receiver-' + uuid.uuid4().hex + '.pid'
    remote_archive = '/opt/camera-ai-lcd.tar.gz'
    package_dir = '/opt/camera_ai_lcd'

    def console(command, timeout=60, check=True):
        return subprocess.run([sys.executable, str(helper), '--port', args.port,
                               '--timeout', str(timeout), command], check=check)

    # System firmware is managed separately; register only the application init entry.
    console('test "$(uname -s)" = Linux && test -x /usr/bin/tcpsvd && '
            'test -x /bin/tar && test -x /usr/bin/sha256sum')
    console('! netstat -ltn 2>/dev/null | grep -q ":18889 "')
    receiver_command = 'cat > ' + remote_archive
    try:
        console('mkdir -p ' + package_dir + ' && { tcpsvd -c 1 ' + board_ip +
                ' 18889 sh -c ' + shlex.quote(receiver_command) +
                ' > /tmp/cv610-ai-receiver.log 2>&1 & echo $! > ' + pidfile +
                '; }; sleep 1; test -s ' + pidfile + ' && kill -0 $(cat ' + pidfile + ')')
        with socket.create_connection((board_ip, 18889), timeout=10) as connection:
            connection.settimeout(120)
            with archive.open('rb') as stream:
                while block := stream.read(262144):
                    connection.sendall(block)
            connection.shutdown(socket.SHUT_WR)
            while connection.recv(1024):
                pass
        console("printf '%s\\n' '" + digest + '  ' + remote_archive +
                "' | sha256sum -c - && tar xzf " + remote_archive + ' -C ' +
                package_dir + ' && cd ' + package_dir +
                ' && sha256sum -c SHA256SUMS > /tmp/cv610-ai-files-check.log && sync', timeout=180)
    finally:
        console('if test -s ' + pidfile + '; then kill $(cat ' + pidfile +
                ') 2>/dev/null || true; rm -f ' + pidfile + '; fi', check=False)
    if not args.no_autostart:
        console('sh ' + package_dir + '/enable-autostart.sh enable')
    print('Installed and SHA256 verified: ' + package_dir)
    if args.no_autostart:
        print('Board command: sh ' + package_dir + '/start-application.sh')
    else:
        print('Boot autostart enabled. Start now: sh ' + package_dir + '/camera-service.sh start')


if __name__ == '__main__':
    main()
