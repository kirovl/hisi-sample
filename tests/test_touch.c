/* Real evdev packets replayed through a nonblocking pipe, including 90-degree rotation. */
#include "../src/touch.c"
#include <assert.h>
int cam_snapshot_hit(int x,int y,uint32_t width){(void)x;(void)y;(void)width;return 0;}
static int writer;
static void emit(int type,int code,int value)
{struct input_event e={.type=type,.code=code,.value=value};assert(write(writer,&e,sizeof(e))==sizeof(e));}
static void point(int x,int y,int down)
{
    emit(EV_ABS,ABS_X,239-y);emit(EV_ABS,ABS_Y,x);
    emit(EV_KEY,BTN_TOUCH,down);emit(EV_SYN,SYN_REPORT,0);
}
int main(void)
{
    int fds[2];assert(pipe(fds)==0);writer=fds[1];fcntl(fds[0],F_SETFL,O_NONBLOCK);
    struct cam_touch t={.fd=fds[0],.rotation=90,.ax={.minimum=0,.maximum=239},.ay={.minimum=0,.maximum=319}};
    int x,y,dy;
    point(80,160,1);assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==0); /* No premature selection. */
    point(82,72,1);point(82,72,0);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==2&&x==80&&y==160&&dy==-88);
    point(30,90,1);point(31,91,0);point(90,90,1);point(90,90,0);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==1&&x==30&&y==90);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==1&&x==90); /* Do not lose next tap in same read. */
    point(80,70,1);point(80,165,1);point(80,165,0);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==2&&dy==95);
    point(30,90,1);point(100,90,1);point(30,90,1);point(30,90,0);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==0); /* Drag out and back is not a tap. */
    point(30,90,1);emit(EV_SYN,SYN_DROPPED,0);point(40,100,1);point(40,100,0);
    assert(cam_touch_poll_gesture(&t,&x,&y,&dy)==0);
    cam_touch_event motion;
    point(302,120,1);assert(cam_touch_poll_motion(&t,&motion)==4&&motion.start_x==302&&motion.x==302);
    point(250,80,1);assert(cam_touch_poll_motion(&t,&motion)==4&&motion.start_x==302&&motion.x==250);
    point(250,80,0);assert(cam_touch_poll_motion(&t,&motion)==3&&motion.y==80);
    puts("Touch release, taps, vertical swipes, rotated axes and dropped events: PASS");
    close(fds[0]);close(fds[1]);return 0;
}
