/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

static int read_text(const char *path,char *out,size_t cap)
{
    FILE *f=fopen(path,"r");if(!f){snprintf(out,cap,"UNAVAILABLE");return -1;}
    size_t n=fread(out,1,cap-1,f);fclose(f);out[n]=0;
    while(n&&(out[n-1]=='\n'||out[n-1]=='\r'))out[--n]=0;
    return n?0:-1;
}
int cam_interface_ipv4(const char *name,char *address,size_t cap)
{
    int fd=socket(AF_INET,SOCK_DGRAM,0);struct ifreq req={0};int rc=-1;
    if(fd<0)return -1;
    snprintf(req.ifr_name,sizeof(req.ifr_name),"%s",name);
    if(ioctl(fd,SIOCGIFADDR,&req)==0&&
        inet_ntop(AF_INET,&((struct sockaddr_in *)&req.ifr_addr)->sin_addr,address,cap))rc=0;
    close(fd);return rc;
}
static void line(cam_device_info *info,const char *format,...)
{
    if(info->count>=12)return;
    va_list args;va_start(args,format);vsnprintf(info->lines[info->count++],53,format,args);va_end(args);
}
static void interface_info(cam_device_info *info,const char *name,const char *label)
{
    char path[128],value[96],ip[32];
    snprintf(path,sizeof(path),"/sys/class/net/%s/carrier",name);
    int ok=read_text(path,value,sizeof(value))==0;
    line(info,"%s: %s",label,ok?(value[0]=='1'?"LINK UP":"LINK DOWN"):"NOT AVAILABLE");
    line(info,"IP   %s",cam_interface_ipv4(name,ip,sizeof(ip))==0?ip:"NO IPV4 ADDRESS");
    snprintf(path,sizeof(path),"/sys/class/net/%s/address",name);read_text(path,value,sizeof(value));
    line(info,"MAC  %s",value);
}
static void status_value(const char *text,const char *key,char *out,size_t cap)
{
    size_t len=strlen(key);out[0]=0;
    for(const char *p=text;*p;){while(*p=='>'||*p==' ')p++;
        const char *end=strchr(p,'\n');if(!end)end=p+strlen(p);
        if(!strncmp(p,key,len)){size_t n=(size_t)(end-p)-len;if(n>=cap)n=cap-1;memcpy(out,p+len,n);out[n]=0;return;}
        p=*end?end+1:end;
    }
}
void cam_device_info_read(cam_device_info *info,int page,const cam_stats *stats)
{
    memset(info,0,sizeof(*info));
    if(page==0){
        interface_info(info,"wlan0","WIFI");
        char output[2048]={0},ssid[64],state[64];const char *cli=getenv("WPA_CLI");
        const char *ctrl=getenv("WPA_CTRL_DIR");
        char *argv[]={(char *)(cli?cli:"/opt/camera_ai_lcd/wifi-runtime/bin/wpa_cli"),"-i","wlan0","-p",(char *)(ctrl?ctrl:"/etc/wireless/wpa_supplicant"),"status",NULL};
        cam_run_capture(argv,NULL,output,sizeof(output),1000);
        status_value(output,"ssid=",ssid,sizeof(ssid));status_value(output,"wpa_state=",state,sizeof(state));
        line(info,"SSID %s",ssid[0]?ssid:"NOT CONNECTED");line(info,"AUTH %s",state[0]?state:"UNAVAILABLE");
        interface_info(info,"eth0","ETHERNET");
    }else if(page==1){
        struct utsname u;char value[160];
        line(info,"BOARD: CV610 EV BOARD");line(info,"SOC: HI3516CV610");
        if(uname(&u)==0){line(info,"KERNEL: %s",u.release);line(info,"CPU ARCH: %s",u.machine);}
        if(read_text("/proc/device-tree/model",value,sizeof(value))==0)line(info,"MODEL: %.44s",value);
        FILE *f=fopen("/proc/meminfo","r");unsigned long kb=0;if(f){fscanf(f,"MemTotal: %lu",&kb);fclose(f);}
        line(info,"LINUX RAM: %lu MB",kb/1024);line(info,"BOARD DDR: 128 MB (CONFIGURED)");
        double seconds=0;f=fopen("/proc/uptime","r");if(f){fscanf(f,"%lf",&seconds);fclose(f);}
        line(info,"UPTIME: %lu MIN",(unsigned long)seconds/60);
    }else if(page==2){
        line(info,"SENSOR: OS04D10 (CONFIGURED)");line(info,"INPUT: SENSOR0 MIPI-CSI 2-LANE");
        if(stats&&stats->sensor_width){line(info,"LIVE: %u X %u",stats->sensor_width,stats->sensor_height);line(info,"SENSOR FPS: %.1f",stats->sensor_fps);line(info,"APP FPS: %.1f",stats->processing_fps);}
        else line(info,"CAMERA: NO LIVE FRAME");
        line(info,"LCD: JD9853 240 X 320");line(info,"CANVAS: 320 X 240 / ROTATE 90");
        line(info,"TOUCH: %s",stats&&stats->touch_ready?"READY":"NOT AVAILABLE");
    }else{
        line(info,"RADIO MODULE: WS73 (CONFIGURED)");
        char value[80];int available=read_text("/sys/class/net/wlan0/address",value,sizeof(value))==0;
        line(info,"WIFI INTERFACE: %s",available?"READY":"NOT AVAILABLE");
        line(info,"WIFI BAND: 2.4 GHZ");
        DIR *d=opendir("/sys/class/bluetooth");struct dirent *entry;int hci=0;
        if(d){while((entry=readdir(d)))if(!strncmp(entry->d_name,"hci",3))hci++;closedir(d);}
        line(info,"BLUETOOTH HCI: %s",hci?"AVAILABLE":"NOT EXPOSED");
        line(info,hci?"BLUETOOTH PAIRING: NOT CHECKED":"BLUETOOTH STACK: NOT VERIFIED");
        line(info,"NO BLUETOOTH SCAN PERFORMED");
    }
}
void cam_device_info_draw(cam_canvas *canvas,const cam_device_info *info,int page)
{
    const uint32_t bg=0x000b1823,panel=0x00162936,text=0x00e5f3f5,active=0x000c6873;
    cam_canvas_clear(canvas,bg);cam_canvas_rect(canvas,0,0,320,29,panel);
    cam_canvas_text(canvas,8,10,"DEVICE INFO",text);
    cam_canvas_rect(canvas,251,4,65,21,active);cam_canvas_text(canvas,271,10,"BACK",text);
    static const char *tabs[]={"NETWORK","BOARD","CAMERA","RADIO"};
    for(int i=0;i<4;i++){cam_canvas_rect(canvas,i*80+2,34,76,24,page==i?active:panel);cam_canvas_text(canvas,i*80+9,42,tabs[i],text);}
    for(unsigned i=0;i<info->count;i++)cam_canvas_text(canvas,7,67+(int)i*18,info->lines[i],text);
    cam_canvas_text(canvas,7,231,"LIVE STATUS / TAP TABS TO SWITCH",0x0089aab2);
}
