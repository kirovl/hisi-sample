/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct cam_touch {
    int fd, rotation, x, y, down, latched, have_x, have_y;
    int start_x, start_y, last_x, last_y, moved, dropped;
    struct input_absinfo ax, ay;
};
static int select_device(struct cam_touch *touch, const char *path)
{
    char name[128] = {0};
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
        ioctl(fd, EVIOCGABS(ABS_X), &touch->ax) < 0 ||
        ioctl(fd, EVIOCGABS(ABS_Y), &touch->ay) < 0 ||
        touch->ax.maximum <= touch->ax.minimum || touch->ay.maximum <= touch->ay.minimum) {
        close(fd); return -1;
    }
    if (strstr(name, "hyn") == NULL && strstr(name, "Hyn") == NULL && strstr(name, "cst") == NULL) {
        close(fd); return -1;
    }
    touch->fd = fd;
    printf("TOUCH %s name=%s axes=%d..%d,%d..%d rotation=%d\n", path, name,
        touch->ax.minimum, touch->ax.maximum, touch->ay.minimum, touch->ay.maximum, touch->rotation);
    return 0;
}
int cam_touch_open(struct cam_touch **output, const char *device, int rotation)
{
    if (output == NULL || (rotation != 0 && rotation != 90 && rotation != 270)) return -1;
    *output = NULL;
    struct cam_touch *touch = calloc(1, sizeof(*touch));
    if (touch == NULL) return -1;
    touch->fd = -1; touch->rotation = rotation;
    if (device != NULL) {
        if (select_device(touch, device) != 0) { free(touch); return -1; }
    } else {
        for (int i = 0; i < 32 && touch->fd < 0; ++i) {
            char path[64]; snprintf(path, sizeof(path), "/dev/input/event%d", i);
            select_device(touch, path);
        }
    }
    if (touch->fd < 0) { free(touch); return -1; }
    *output = touch; return 0;
}
static int scale_axis(int value, const struct input_absinfo *axis, int size)
{
    if (value < axis->minimum) value = axis->minimum;
    if (value > axis->maximum) value = axis->maximum;
    return (int)((int64_t)(value - axis->minimum) * (size - 1) / (axis->maximum - axis->minimum));
}
/* Emit once on release, so dragging a list never selects the first touched AP. */
static int poll_input(struct cam_touch *touch, int *x_out, int *y_out, int *dy_out,int motion)
{
    if (touch == NULL) return 0;
    struct input_event event;
    ssize_t length;
    while ((length = read(touch->fd, &event, sizeof(event))) > 0) {
        if (length != sizeof(event)) return -1;
        if (event.type == EV_ABS && event.code == ABS_X) { touch->x = event.value; touch->have_x = 1; }
        if (event.type == EV_ABS && event.code == ABS_Y) { touch->y = event.value; touch->have_y = 1; }
        if (event.type == EV_KEY && event.code == BTN_TOUCH) touch->down = event.value != 0;
        if (event.type == EV_SYN && event.code == SYN_DROPPED) {
            touch->down = touch->latched = touch->have_x = touch->have_y = 0;
            touch->moved = 0; touch->dropped = 1;
            /* Discard the remainder of this incomplete contact until release. */
            continue;
        }
        if (event.type != EV_SYN || event.code != SYN_REPORT) continue;
        if (touch->dropped) {
            if (!touch->down) { touch->dropped=0; touch->have_x=touch->have_y=0; }
            continue;
        }
        if (touch->down && touch->have_x && touch->have_y) {
            int px = scale_axis(touch->x, &touch->ax, 240);
            int py = scale_axis(touch->y, &touch->ay, 320);
            int x = px, y = py;
            if (touch->rotation == 90) { x = py; y = 239 - px; }
            if (touch->rotation == 270) { x = 319 - py; y = px; }
            if (!touch->latched) {
                touch->start_x = x; touch->start_y = y;
                touch->latched = 1; touch->moved = 0;
            }
            touch->last_x = x; touch->last_y = y;
            if (abs(x - touch->start_x) >= 24 || abs(y - touch->start_y) >= 24) touch->moved = 1;
            if(motion){if(x_out)*x_out=touch->start_x;if(y_out)*y_out=touch->start_y;return 4;}
        } else if (!touch->down && touch->latched) {
            touch->latched = 0;
            int dx = touch->last_x - touch->start_x, dy = touch->last_y - touch->start_y;
            if (x_out) *x_out = touch->start_x;
            if (y_out) *y_out = touch->start_y;
            if (dy_out) *dy_out = dy;
            if (!touch->moved) return 1;
            if (abs(dy) >= 24 && abs(dy) > abs(dx)) return 2;
            return 3;
        }
    }
    return length < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ? -1 : 0;
}
int cam_touch_poll_gesture(struct cam_touch *touch,int *x,int *y,int *dy)
{
    int result=poll_input(touch,x,y,dy,0);return result==3?0:result;
}
int cam_touch_poll_motion(struct cam_touch *touch,cam_touch_event *event)
{
    if(!event)return -1;
    int dy=0;memset(event,0,sizeof(*event));
    int result=poll_input(touch,&event->start_x,&event->start_y,&dy,1);
    event->type=result;
    if(touch){event->x=touch->last_x;event->y=touch->last_y;}
    return result;
}
int cam_touch_poll_event(struct cam_touch *touch, int *x_out, int *y_out)
{
    int dy = 0;
    int result = cam_touch_poll_gesture(touch, x_out, y_out, &dy);
    return result == 2 ? 0 : result;
}
int cam_touch_poll(struct cam_touch *touch)
{
    int x = 0, y = 0;
    int event = cam_touch_poll_event(touch, &x, &y);
    if (event <= 0) return event;
    int hit = cam_snapshot_hit(x, y, touch != NULL && touch->rotation == 0 ? 240 : 320);
    printf("TOUCH_POINT x=%d y=%d screenshot=%d\n", x, y, hit);
    return hit;
}
void cam_touch_close(struct cam_touch *touch)
{
    if (touch != NULL) { if (touch->fd >= 0) close(touch->fd); free(touch); }
}
