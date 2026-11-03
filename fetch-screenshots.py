#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Download CV610 PNG screenshots and JSON metadata over Ethernet; UART sends commands."""
import argparse
import hashlib
import ipaddress
import pathlib
import re
import shlex
import socket
import subprocess
import sys
import tarfile
import tempfile
import uuid

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board-ip',required=True)
    p.add_argument('--port',default='/dev/ttyACM0')
    p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('screenshots'))
    args=p.parse_args();ip=str(ipaddress.IPv4Address(args.board_ip))
    helper=pathlib.Path(__file__).parent/'tools'/'board-console.py'
    token=uuid.uuid4().hex
    archive='/tmp/camera-shots-'+token+'.tar.gz'
    pidfile='/tmp/camera-shots-'+token+'.pid'
    def console(command):
        r=subprocess.run([sys.executable,str(helper),'--port',args.port,'--timeout','60',command],capture_output=True,check=True)
        return r.stdout.decode(errors='replace')
    output=console('cd /opt/camera_ai_lcd/screenshots && test -n "$(ls *.png 2>/dev/null)" && tar czf '+archive+' . && sha256sum '+archive)
    match=re.search(r'(?m)^([0-9a-f]{64})\s+'+re.escape(archive)+r'\r?$',output)
    if match is None:raise RuntimeError('Cannot confirm screenshot archive SHA256')
    try:
        console('! netstat -ltn 2>/dev/null | grep -q ":18889 " && { tcpsvd -c 1 '+ip+' 18889 sh -c '+shlex.quote('cat '+archive)+' >/tmp/camera-shots-transfer.log 2>&1 & echo $! > '+pidfile+'; }; sleep 1; kill -0 $(cat '+pidfile+')')
        with tempfile.TemporaryDirectory(prefix='cv610-shots-') as tmp:
            local=pathlib.Path(tmp)/'screenshots.tar.gz'
            with socket.create_connection((ip,18889),10) as s,local.open('wb') as f:
                s.settimeout(120)
                while block:=s.recv(262144):f.write(block)
            if hashlib.sha256(local.read_bytes()).hexdigest()!=match.group(1):raise RuntimeError('Screenshot archive SHA256 mismatch')
            args.output.mkdir(parents=True,exist_ok=True)
            with tarfile.open(local,'r:gz') as tf:tf.extractall(args.output,filter='data')
    finally:
        console('if test -s '+pidfile+'; then kill $(cat '+pidfile+') 2>/dev/null || true; fi; rm -f '+pidfile+' '+archive)
    for path in sorted(args.output.glob('shot-*.png')):print(path.resolve())
    print('SHA256 verified. PNG images include the same camera picture, boxes and labels as the LCD.')
if __name__=='__main__':main()
