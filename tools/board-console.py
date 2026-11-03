#!/usr/bin/env python3
"""Run a Linux-shell command over CV610 debug UART; record output and exit status."""
import argparse,os,re,select,sys,termios,time,uuid
p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/ttyACM0');p.add_argument('--timeout',type=float,default=60);p.add_argument('command');a=p.parse_args()
fd=os.open(a.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
try:
 attrs=termios.tcgetattr(fd);attrs[0]=attrs[1]=attrs[3]=0;attrs[2]=termios.CS8|termios.CLOCAL|termios.CREAD;attrs[4]=attrs[5]=termios.B115200;attrs[6][termios.VMIN]=0;attrs[6][termios.VTIME]=0;termios.tcsetattr(fd,termios.TCSANOW,attrs)
 os.write(fd,bytes([13]));time.sleep(.3)
 while select.select([fd],[],[],.1)[0]:os.read(fd,8192)
 marker='__AI_'+uuid.uuid4().hex+'__'
 # Marker is assembled by printf so terminal echo cannot masquerade as completion.
 command=a.command+"; __ai_rc=$?; printf '\\n%s%s:%s\\n' '"+marker[:12]+"' '"+marker[12:]+"' \"$__ai_rc\""
 os.write(fd,command.encode()+bytes([13]))
 data=bytearray();end=time.monotonic()+a.timeout;pattern=re.compile(rb'(?:\r?\n)'+marker.encode()+rb':(\d+)\r?\n')
 while time.monotonic()<end:
  if not select.select([fd],[],[],.2)[0]:continue
  chunk=os.read(fd,8192);data.extend(chunk);sys.stdout.buffer.write(chunk);sys.stdout.buffer.flush()
  match=pattern.search(data)
  if match:sys.exit(int(match.group(1)))
 raise SystemExit('Timed out waiting for command completion')
finally:os.close(fd)
