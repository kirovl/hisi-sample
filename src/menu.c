/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <stdlib.h>

static int ball_hit(int x,int y,int bx,int by){return (x-bx)*(x-bx)+(y-by)*(y-by)<=18*18;}
static void position(int width,int height,int *x,int *y)
{
    *x=width-46;
    *y=height>=234?34:height-200;
    if(*y<0)*y=0;
}
static void pixel(cam_canvas *c,int x,int y,uint32_t color)
{
    if(x>=0&&y>=0&&(uint32_t)x<c->width&&(uint32_t)y<c->height)c->pixels[y*c->stride+x]=color;
}
static void circle(cam_canvas *c,int cx,int cy,int r,uint32_t color,int filled)
{
    for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){
        int d=x*x+y*y;if(d<=r*r&&(filled||d>=(r-2)*(r-2)))pixel(c,cx+x,cy+y,color);
    }
}
static void icon(cam_canvas *c,int x,int y,int type,uint32_t color)
{
    if(type==0){ /* Information: encircled i. */
        circle(c,x,y,10,color,0);cam_canvas_rect(c,x-1,y-1,3,8,color);cam_canvas_rect(c,x-1,y-6,3,3,color);
    }else if(type==1){ /* Frame statistics. */
        cam_canvas_rect(c,x-10,y+1,5,9,color);cam_canvas_rect(c,x-2,y-5,5,15,color);cam_canvas_rect(c,x+6,y-10,5,20,color);
    }else if(type==2){ /* Wi-Fi arcs. */
        for(int yy=-12;yy<=0;yy++)for(int xx=-14;xx<=14;xx++){
            int d=xx*xx+yy*yy;
            if(yy<=-abs(xx)/2&&((d>=145&&d<=196)||(d>=49&&d<=81)))pixel(c,x+xx,y+4+yy,color);
        }
        circle(c,x,y+7,2,color,1);
    }else if(type==3){ /* Camera / screenshot. */
        cam_canvas_rect(c,x-12,y-8,25,19,color);cam_canvas_rect(c,x-6,y-11,10,4,color);
        cam_canvas_rect(c,x-10,y-6,21,15,0x001b3244);circle(c,x,y+1,6,color,0);
    }else{ /* AI inference toggle. */
        circle(c,x,y,11,color,0);cam_canvas_rect(c,x-7,y-1,14,3,color);
        cam_canvas_rect(c,x-2,y-7,4,14,color);circle(c,x,y,3,color,1);
    }
}
int cam_menu_event(cam_menu *m,const cam_touch_event *e,uint32_t width,uint32_t height)
{
    if(e->type<=0)return 0;
    if(e->type!=1)return 0;
    if(ball_hit(e->x,e->y,m->x,m->y)){m->open=!m->open;return 0;}
    if(m->open){
        int x,y;position((int)width,(int)height,&x,&y);m->open=0;
        if(e->x>=x&&e->x<x+46&&e->y>=y&&e->y<y+200){
            int action=(e->y-y)/40+1;
            if(action==2)m->show_stats=!m->show_stats;
            if(action==5)m->ai_enabled=!m->ai_enabled;
            return action;
        }
    }
    return 0;
}
void cam_menu_draw(cam_canvas *c,const cam_menu *m)
{
    if(!c||!c->pixels)return;
    /* Fixed translucent handle; the action strip opens downward. */
    for(int y=-14;y<=14;y++)for(int x=-14;x<=14;x++)if(x*x+y*y<=14*14){
        int px=m->x+x,py=m->y+y;if(px<0||py<0||(uint32_t)px>=c->width||(uint32_t)py>=c->height)continue;
        uint32_t old=c->pixels[py*c->stride+px];
        uint32_t shade=((old&0x00fefefe)>>1)+0x00101c24;pixel(c,px,py,shade);
    }
    for(int i=0;i<3;i++)cam_canvas_rect(c,m->x-7,m->y-5+i*5,15,2,0x00e5f3f5);
    if(m->open){
        int x,y;position(c->width,c->height,&x,&y);cam_canvas_rect(c,x,y,46,200,0x001b3244);
        for(int i=0;i<5;i++){
            if(i==1&&m->show_stats)cam_canvas_rect(c,x+2,y+i*40+2,42,36,0x000c6873);
            if(i==4&&m->ai_enabled)cam_canvas_rect(c,x+2,y+i*40+2,42,36,0x000c6873);
            icon(c,x+23,y+20+i*40,i,0x00e5f3f5);
        }
    }
}
