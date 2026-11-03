#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a separate static BusyBox with read-only FTP and static HTTP; never modify SDK source."""
import argparse
import pathlib
import re
import shutil
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path)
    args=p.parse_args();sdk=args.sdk.resolve(strict=True);out=args.output.resolve()
    source=sdk/'third_party/busybox/busybox-1.34.1'
    prefix=str(sdk/'prebuilts/gcc-10.3-arm-musl-x86-linux-26.06.1/arm-v01c02-linux-musleabi-gcc/bin/arm-v01c02-linux-musleabi-')
    if not pathlib.Path(prefix+'gcc').is_file():raise SystemExit('ARM musl toolchain not found')
    out.mkdir(parents=True,exist_ok=True)
    work=out/'busybox-source'
    marker=out/'camera-services-build.marker'
    if work.exists() and not marker.is_file():raise SystemExit('Refusing to reuse an unrecognized source directory')
    if not work.exists():shutil.copytree(source,work,symlinks=True);marker.write_text('camera_ai_lcd service build\n')
    def make(*targets,input=None):
        subprocess.run(['make','-C',str(work),'CROSS_COMPILE='+prefix,*targets],input=input,text=True,check=True)
    make('distclean');make('allnoconfig')
    config=work/'.config';text=config.read_text()
    enabled=['STATIC','LFS','FTPD','HTTPD','TCPSVD','LS','FEATURE_LS_TIMESTAMPS',
             'LONG_OPTS','SHOW_USAGE','FEATURE_VERBOSE_USAGE','FEATURE_HTTPD_RANGES','FEATURE_HTTPD_SETUID']
    disabled=['FEATURE_FTPD_WRITE','FEATURE_FTPD_AUTHENTICATION','FEATURE_HTTPD_CGI']
    for name,value in [(name,True) for name in enabled]+[(name,False) for name in disabled]:
        key='CONFIG_'+name
        replacement=key+'=y' if value else '# '+key+' is not set'
        text,count=re.subn(r'(?m)^(?:'+key+r'=.*|# '+key+r' is not set)$',replacement,text)
        if count!=1:raise SystemExit('Missing or duplicate BusyBox option '+key)
    config.write_text(text)
    make('oldconfig',input='\n'*256);make('-j8')
    for name in enabled:
        if 'CONFIG_'+name+'=y' not in config.read_text().splitlines():raise SystemExit('Required option not enabled: '+name)
    for name in disabled:
        if 'CONFIG_'+name+'=y' in config.read_text().splitlines():raise SystemExit('Unexpected writable/CGI feature enabled')
    shutil.copyfile(work/'busybox',out/'busybox')
    (out/'busybox').chmod(0o755)
    shutil.copyfile(work/'LICENSE',out/'BUSYBOX-LICENSE')
    shutil.copyfile(config,out/'busybox.config')
    print('Built: '+str(out/'busybox'))
    print('BusyBox 1.34.1; corresponding source: '+str(source))
if __name__=='__main__':main()
