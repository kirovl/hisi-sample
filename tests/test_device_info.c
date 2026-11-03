#include "camera_ai_lcd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Avoid commands/network hardware in rendering checks. */
int cam_run_capture(char *const argv[],const char *input,char *output,size_t cap,int timeout)
{(void)argv;(void)input;(void)timeout;snprintf(output,cap,"wpa_state=COMPLETED\nssid=Test Network\n");return 0;}
int main(int argc,char **argv)
{
    uint32_t pixels[320*240];cam_canvas canvas={pixels,320,240,320,320*240};
    cam_stats stats={.sensor_width=2560,.sensor_height=1440,.sensor_fps=30,.processing_fps=6,.touch_ready=1};
    for(int page=0;page<4;page++){
        cam_device_info info;cam_device_info_read(&info,page,&stats);assert(info.count>0&&info.count<=9);
        if(page==0)assert(!strcmp(info.lines[3],"SSID Test Network"));
        cam_device_info_draw(&canvas,&info,page);
        if(argc>1){char path[256];snprintf(path,sizeof(path),"%s-%d.png",argv[1],page);assert(cam_save_png(path,&canvas)==0);}
    }
    puts("Device status and all four information pages: PASS");return 0;
}
