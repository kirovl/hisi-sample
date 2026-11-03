/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CAMERA_AI_LCD_H
#define CAMERA_AI_LCD_H

#include <stddef.h>
#include <stdint.h>

#define CAM_MAX_OBJECTS 320U
#define CAM_MAX_IMAGE_SIZE 4096U

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_y;
    uint32_t stride_vu;
    uint8_t *y;
    uint8_t *vu;
} cam_image;

typedef struct {
    int type;
    float confidence;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} cam_object;

typedef struct {
    cam_object objects[CAM_MAX_OBJECTS];
    size_t count;
    unsigned int dropped;
} cam_result;

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    size_t capacity;
} cam_canvas;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} cam_viewport;

typedef struct {
    uint32_t sensor_width, sensor_height;
    double sensor_fps;
    double processing_fps;
    unsigned int saved;
    int touch_ready;
    int show_overlay;
    int ai_enabled;
} cam_stats;

struct cam_touch;
struct cam_camera;
struct cam_detector;
struct cam_display;
/* UI state contains no passwords. Network/hardware values are refreshed on demand. */
typedef struct { char lines[12][53]; unsigned int count; } cam_device_info;
typedef struct { int x,y,open,show_stats,ai_enabled; } cam_menu;
typedef struct { int type,start_x,start_y,x,y; } cam_touch_event;
void cam_menu_draw(cam_canvas *canvas,const cam_menu *menu);
/* Returns 1=info, 2=statistics, 3=Wi-Fi, 4=snapshot, 5=AI; updates floating position. */
int cam_menu_event(cam_menu *menu,const cam_touch_event *event,uint32_t width,uint32_t height);
int cam_touch_poll_motion(struct cam_touch *touch,cam_touch_event *event);
int cam_interface_ipv4(const char *name, char *address, size_t capacity);
void cam_device_info_read(cam_device_info *info, int page, const cam_stats *stats);
void cam_device_info_draw(cam_canvas *canvas, const cam_device_info *info, int page);
int cam_wifi_setup(struct cam_display *display, struct cam_touch *touch, int (*cancelled)(void));
int cam_wifi_start_services(void);
int cam_run_capture(char *const argv[], const char *input, char *output, size_t cap, int timeout_ms);


int cam_image_valid(const cam_image *image);
int cam_resize_nv21(const cam_image *source, cam_image *destination);
int cam_view_fit(uint32_t width, uint32_t height, uint32_t canvas_width,
    uint32_t canvas_height, cam_viewport *viewport);
int cam_view_fill(uint32_t width, uint32_t height, uint32_t canvas_width,
    uint32_t canvas_height, cam_viewport *viewport);
int cam_map_box(const cam_object *object, const cam_image *image,
    const cam_viewport *viewport, cam_viewport *box);
int cam_render(cam_canvas *canvas, const cam_image *image, const cam_result *result, double fps);
int cam_render_ui(cam_canvas *canvas, const cam_image *image, const cam_result *result, const cam_stats *stats);
int cam_rotate_to_panel(const cam_canvas *source, cam_canvas *panel, int rotation);
int cam_snapshot_hit(int x, int y, uint32_t width);
int cam_touch_open(struct cam_touch **touch, const char *device, int rotation);
int cam_touch_poll(struct cam_touch *touch);
int cam_touch_poll_gesture(struct cam_touch *touch, int *x, int *y, int *dy);
int cam_touch_poll_event(struct cam_touch *touch, int *x, int *y);
void cam_touch_close(struct cam_touch *touch);
int cam_canvas_clear(cam_canvas *canvas, uint32_t color);
int cam_canvas_rect(cam_canvas *canvas, int x, int y, int width, int height, uint32_t color);
int cam_canvas_text(cam_canvas *canvas, int x, int y, const char *text, uint32_t color);
int cam_save_png(const char *path, const cam_canvas *canvas);
int cam_save_capture(const char *directory, const cam_canvas *canvas, const cam_image *image,
    const cam_result *result, const cam_stats *stats, unsigned int sequence);
void cam_camera_stats(struct cam_camera *camera, cam_stats *stats);
const char *cam_class_name(int type);

int cam_camera_open(struct cam_camera **camera);
int cam_camera_get(struct cam_camera *camera, cam_image *image);
void cam_camera_release(struct cam_camera *camera);
void cam_camera_close(struct cam_camera *camera);
int cam_detector_open(struct cam_detector **detector, const char *model_dir, int all_models, float threshold);
int cam_detector_process(struct cam_detector *detector, const cam_image *image, cam_result *result);
void cam_detector_close(struct cam_detector *detector);
int cam_display_open(struct cam_display **display, const char *device, int rotation);
cam_canvas *cam_display_canvas(struct cam_display *display);
int cam_display_present(struct cam_display *display);
void cam_display_close(struct cam_display *display);

#endif
