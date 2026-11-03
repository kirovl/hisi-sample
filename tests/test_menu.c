/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tap(cam_menu *m,int x,int y){cam_touch_event e={.type=1,.x=x,.y=y};return cam_menu_event(m,&e,320,240);}
int main(void)
{
    cam_menu m={.x=296,.y=16,.ai_enabled=1};
    assert(!m.show_stats);tap(&m,296,16);assert(m.open);
    assert(tap(&m,297,94)==2&&m.show_stats&&!m.open);
    tap(&m,296,16);assert(tap(&m,297,94)==2&&!m.show_stats);
    tap(&m,296,16);assert(tap(&m,297,54)==1);
    tap(&m,296,16);assert(tap(&m,297,134)==3);
    tap(&m,296,16);assert(tap(&m,297,174)==4);
    tap(&m,296,16);assert(tap(&m,297,214)==5&&!m.ai_enabled);
    tap(&m,296,16);assert(tap(&m,297,214)==5&&m.ai_enabled);
    tap(&m,296,16);tap(&m,1,1);assert(!m.open);
    /* Fixed top-right handle and downward menu stay inside the framebuffer. */
    uint32_t guard[320*240+2];memset(guard,0,sizeof(guard));guard[0]=guard[320*240+1]=0x12345678;
    cam_canvas c={.pixels=guard+1,.width=320,.height=240,.stride=320,.capacity=320*240};
    m.open=1;cam_menu_draw(&c,&m);assert(tap(&m,297,94)==2&&m.show_stats);
    assert(guard[0]==0x12345678&&guard[320*240+1]==0x12345678);
    m.x=296;m.y=16;m.open=1;
    assert(tap(&m,297,174)==4);
    unsigned char y[16*16],vu[16*8];memset(y,90,sizeof(y));memset(vu,128,sizeof(vu));
    cam_image im={16,16,16,16,y,vu};cam_result result={0};cam_stats stats={.sensor_width=2560,.sensor_height=1440,.sensor_fps=15,.processing_fps=2.4,.ai_enabled=1};
    assert(cam_render_ui(&c,&im,&result,&stats)==0);uint32_t before[320*240];memcpy(before,c.pixels,sizeof(before));
    stats.show_overlay=1;assert(cam_render_ui(&c,&im,&result,&stats)==0);assert(memcmp(before,c.pixels,sizeof(before))!=0);
    stats.ai_enabled=0;assert(cam_render_ui(&c,&im,&result,&stats)==0);
    puts("Menu actions, AI/statistics toggle, fixed layout and edge rendering: PASS");
}
