/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CAM_TOP_HEIGHT 40U
#define CAM_BOTTOM_HEIGHT 20U
#define CAM_WHITE 0x00ffffffU
#define CAM_BLACK 0x00000000U

/* Five columns per glyph, top row in the least significant bit. */
static const uint8_t g_font[36][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

static const uint8_t g_lower[26][5] = {
    {0x20,0x54,0x54,0x54,0x78}, {0x7f,0x48,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7f},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7e,0x09,0x01,0x02},
    {0x0c,0x52,0x52,0x52,0x3e}, {0x7f,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7d,0x40,0x00}, {0x20,0x40,0x44,0x3d,0x00},
    {0x7f,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7f,0x40,0x00},
    {0x7c,0x04,0x18,0x04,0x78}, {0x7c,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38}, {0x7c,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7c}, {0x7c,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3f,0x44,0x40,0x20},
    {0x3c,0x40,0x40,0x20,0x7c}, {0x1c,0x20,0x40,0x20,0x1c},
    {0x3c,0x40,0x30,0x40,0x3c}, {0x44,0x28,0x10,0x28,0x44},
    {0x0c,0x50,0x50,0x50,0x3c}, {0x44,0x64,0x54,0x4c,0x44}
};

int cam_image_valid(const cam_image *image)
{
    return image != NULL && image->y != NULL && image->vu != NULL &&
        image->width >= 2 && image->height >= 2 &&
        image->width <= CAM_MAX_IMAGE_SIZE && image->height <= CAM_MAX_IMAGE_SIZE &&
        (image->width & 1U) == 0 && (image->height & 1U) == 0 &&
        image->stride_y >= image->width && image->stride_y <= CAM_MAX_IMAGE_SIZE * 4U &&
        image->stride_vu >= image->width && image->stride_vu <= CAM_MAX_IMAGE_SIZE * 4U;
}

int cam_resize_nv21(const cam_image *source, cam_image *destination)
{
    if (!cam_image_valid(source) || !cam_image_valid(destination)) {
        return -1;
    }
    for (uint32_t y = 0; y < destination->height; y++) {
        uint32_t sy = (uint64_t)y * source->height / destination->height;
        for (uint32_t x = 0; x < destination->width; x++) {
            uint32_t sx = (uint64_t)x * source->width / destination->width;
            destination->y[(size_t)y * destination->stride_y + x] =
                source->y[(size_t)sy * source->stride_y + sx];
        }
    }
    for (uint32_t y = 0; y < destination->height / 2U; y++) {
        uint32_t sy = (uint64_t)y * (source->height / 2U) / (destination->height / 2U);
        for (uint32_t x = 0; x < destination->width; x += 2) {
            uint32_t sx = ((uint64_t)x * source->width / destination->width) & ~1U;
            size_t dst = (size_t)y * destination->stride_vu + x;
            size_t src = (size_t)sy * source->stride_vu + sx;
            destination->vu[dst] = source->vu[src];
            destination->vu[dst + 1] = source->vu[src + 1];
        }
    }
    return 0;
}

int cam_view_fit(uint32_t width, uint32_t height, uint32_t canvas_width,
    uint32_t canvas_height, cam_viewport *viewport)
{
    if (viewport == NULL || width == 0 || height == 0 || canvas_width == 0 ||
        canvas_height <= (CAM_TOP_HEIGHT + CAM_BOTTOM_HEIGHT) || width > CAM_MAX_IMAGE_SIZE ||
        height > CAM_MAX_IMAGE_SIZE || canvas_width > CAM_MAX_IMAGE_SIZE || canvas_height > CAM_MAX_IMAGE_SIZE) {
        return -1;
    }
    uint32_t available = canvas_height - (CAM_TOP_HEIGHT + CAM_BOTTOM_HEIGHT);
    viewport->width = canvas_width;
    viewport->height = (uint64_t)height * canvas_width / width;
    if ((uint32_t)viewport->height > available) {
        viewport->height = available;
        viewport->width = (uint64_t)width * available / height;
    }
    if (viewport->width == 0 || viewport->height == 0) {
        return -1;
    }
    viewport->x = ((int)canvas_width - viewport->width) / 2;
    viewport->y = CAM_TOP_HEIGHT + ((int)canvas_height - CAM_TOP_HEIGHT - CAM_BOTTOM_HEIGHT - viewport->height) / 2;
    return 0;
}

/* Fill the entire panel without distortion; centered excess is cropped. */
int cam_view_fill(uint32_t width,uint32_t height,uint32_t cw,uint32_t ch,cam_viewport *v)
{
    if(!v||!width||!height||!cw||!ch||width>CAM_MAX_IMAGE_SIZE||height>CAM_MAX_IMAGE_SIZE||
       cw>CAM_MAX_IMAGE_SIZE||ch>CAM_MAX_IMAGE_SIZE)return -1;
    if((uint64_t)width*ch>=(uint64_t)height*cw){
        v->height=(int)ch;v->width=(int)(((uint64_t)width*ch+height-1)/height);
    }else{
        v->width=(int)cw;v->height=(int)(((uint64_t)height*cw+width-1)/width);
    }
    v->x=((int)cw-v->width)/2;v->y=((int)ch-v->height)/2;
    return 0;
}

int cam_map_box(const cam_object *object, const cam_image *image,
    const cam_viewport *viewport, cam_viewport *box)
{
    if (object == NULL || image == NULL || viewport == NULL || box == NULL ||
        image->width == 0 || image->height == 0 || viewport->width <= 0 || viewport->height <= 0 ||
        object->width <= 0 || object->height <= 0) {
        return -1;
    }
    int64_t left = object->x < 0 ? 0 : object->x;
    int64_t top = object->y < 0 ? 0 : object->y;
    int64_t right = (int64_t)object->x + object->width;
    int64_t bottom = (int64_t)object->y + object->height;
    if (right > image->width) {
        right = image->width;
    }
    if (bottom > image->height) {
        bottom = image->height;
    }
    if (right <= left || bottom <= top) {
        return -1;
    }
    box->x = viewport->x + left * viewport->width / image->width;
    box->y = viewport->y + top * viewport->height / image->height;
    int x2 = viewport->x + (right * viewport->width + image->width - 1) / image->width;
    int y2 = viewport->y + (bottom * viewport->height + image->height - 1) / image->height;
    box->width = x2 - box->x;
    box->height = y2 - box->y;
    return 0;
}

const char *cam_class_name(int type)
{
    switch (type) {
        case 0: return "FACE";
        case 1: return "HUMAN";
        case 2: return "VEHICLE";
        case 3: return "PET";
        case 11: return "PACKAGE";
        default: return "OBJECT";
    }
}

static void put_pixel(cam_canvas *canvas, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && (uint32_t)x < canvas->width && (uint32_t)y < canvas->height) {
        canvas->pixels[(size_t)y * canvas->stride + x] = color;
    }
}

static uint8_t glyph_column(char glyph, unsigned int column)
{
    static const uint8_t bang[5] = {0,0,0x5f,0,0};
    static const uint8_t hash[5] = {0x14,0x7f,0x14,0x7f,0x14};
    static const uint8_t dollar[5] = {0x24,0x2a,0x7f,0x2a,0x12};
    static const uint8_t ampersand[5] = {0x36,0x49,0x55,0x22,0x50};
    static const uint8_t apostrophe[5] = {0,0x05,0x03,0,0};
    static const uint8_t comma[5] = {0,0x50,0x30,0,0};
    static const uint8_t semicolon[5] = {0,0x56,0x36,0,0};
    static const uint8_t less[5] = {0x08,0x14,0x22,0x41,0};
    static const uint8_t greater[5] = {0,0x41,0x22,0x14,0x08};
    static const uint8_t open_bracket[5] = {0,0x7f,0x41,0x41,0};
    static const uint8_t close_bracket[5] = {0,0x41,0x41,0x7f,0};
    static const uint8_t open_brace[5] = {0x08,0x36,0x41,0x41,0};
    static const uint8_t close_brace[5] = {0,0x41,0x41,0x36,0x08};
    static const uint8_t backslash[5] = {0x02,0x04,0x08,0x10,0x20};
    static const uint8_t pipe[5] = {0,0,0x7f,0,0};
    static const uint8_t tilde[5] = {0x08,0x04,0x08,0x10,0x08};
    static const uint8_t at[5] = {0x3e,0x41,0x5d,0x55,0x1e};
    static const uint8_t caret[5] = {0x04,0x02,0x01,0x02,0x04};
    static const uint8_t dash[5] = {0,0,0x08,0,0};
    static const uint8_t underscore[5] = {0x40,0x40,0x40,0x40,0x40};
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t question[5] = {0x02,0x01,0x51,0x09,0x06};
    static const uint8_t star[5] = {0x14,0x08,0x3e,0x08,0x14};
    static const uint8_t plus[5] = {0x08,0x08,0x3e,0x08,0x08};
    static const uint8_t equal[5] = {0x14,0x14,0x14,0x14,0x14};
    static const uint8_t open_paren[5] = {0,0x1c,0x22,0x41,0};
    static const uint8_t close_paren[5] = {0,0x41,0x22,0x1c,0};
    if (glyph >= '0' && glyph <= '9') {
        return g_font[glyph - '0'][column];
    }
    if (glyph >= 'A' && glyph <= 'Z') {
        return g_font[10 + glyph - 'A'][column];
    }
    if (glyph >= 'a' && glyph <= 'z') return g_lower[glyph - 'a'][column];
    if (glyph == '.') {
        return column == 2 ? 0x40 : 0;
    }
    if (glyph == '-') return dash[column];
    if (glyph == '_') return underscore[column];
    if (glyph == ':') return colon[column];
    if (glyph == '/') return slash[column];
    if (glyph == '?') return question[column];
    if (glyph == '*') return star[column];
    if (glyph == '+') return plus[column];
    if (glyph == '=') return equal[column];
    if (glyph == '(') return open_paren[column];
    if (glyph == ')') return close_paren[column];
    if (glyph == '!') return bang[column];
    if (glyph == '#') return hash[column];
    if (glyph == '$') return dollar[column];
    if (glyph == '&') return ampersand[column];
    if (glyph == '\'') return apostrophe[column];
    if (glyph == '`') {
        static const uint8_t grave[5] = {0,0x01,0x02,0x04,0};
        return grave[column];
    }
    if (glyph == '"') {
        static const uint8_t quote[5] = {0,0x07,0,0x07,0};
        return quote[column];
    }
    if (glyph == ',') return comma[column];
    if (glyph == ';') return semicolon[column];
    if (glyph == '<') return less[column];
    if (glyph == '>') return greater[column];
    if (glyph == '[') return open_bracket[column];
    if (glyph == ']') return close_bracket[column];
    if (glyph == '{') return open_brace[column];
    if (glyph == '}') return close_brace[column];
    if (glyph == '\\') return backslash[column];
    if (glyph == '|') return pipe[column];
    if (glyph == '~') return tilde[column];
    if (glyph == '@') return at[column];
    if (glyph == '^') return caret[column];
    if (glyph == ' ') return 0;
    if (glyph == '%') {
        static const uint8_t percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
        return percent[column];
    }
    return 0;
}

int cam_canvas_clear(cam_canvas *canvas, uint32_t color)
{
    if (canvas == NULL || canvas->pixels == NULL || canvas->width == 0 || canvas->height == 0 ||
        canvas->stride < canvas->width || canvas->stride > SIZE_MAX / canvas->height ||
        canvas->capacity < (size_t)canvas->stride * canvas->height) return -1;
    for (uint32_t y = 0; y < canvas->height; ++y)
        for (uint32_t x = 0; x < canvas->width; ++x)
            canvas->pixels[(size_t)y * canvas->stride + x] = color;
    return 0;
}

int cam_canvas_rect(cam_canvas *canvas, int x, int y, int width, int height, uint32_t color)
{
    if (canvas == NULL || canvas->pixels == NULL || canvas->height == 0 || width < 0 || height < 0 ||
        canvas->stride < canvas->width || canvas->stride > SIZE_MAX / canvas->height ||
        canvas->capacity < (size_t)canvas->stride * canvas->height) return -1;
    int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    if (right > canvas->width) right = canvas->width;
    if (bottom > canvas->height) bottom = canvas->height;
    for (int py = top; py < bottom; ++py)
        for (int px = left; px < right; ++px)
            canvas->pixels[(size_t)py * canvas->stride + px] = color;
    return 0;
}

int cam_canvas_text(cam_canvas *canvas, int x, int y, const char *text, uint32_t color)
{
    if (canvas == NULL || canvas->pixels == NULL || text == NULL || canvas->height == 0 ||
        canvas->stride < canvas->width || canvas->stride > SIZE_MAX / canvas->height ||
        canvas->capacity < (size_t)canvas->stride * canvas->height) return -1;
    int cursor = x;
    for (size_t index = 0; text[index] != '\0' && index < 96; ++index) {
        unsigned char ch = (unsigned char)text[index];
        if (ch >= 0x80) {
            if ((ch & 0xc0) != 0x80) {
                for (int row = 0; row < 7; ++row) {
                    put_pixel(canvas, cursor, y + row, color);
                    put_pixel(canvas, cursor + 4, y + row, color);
                }
                for (int column = 0; column < 5; ++column) {
                    put_pixel(canvas, cursor + column, y, color);
                    put_pixel(canvas, cursor + column, y + 6, color);
                }
                cursor += 7;
            }
            continue;
        }
        for (unsigned int column = 0; column < 5; ++column) {
            uint8_t bits = glyph_column((char)ch, column);
            for (unsigned int row = 0; row < 7; ++row)
                if ((bits & (1U << row)) != 0) put_pixel(canvas, cursor + (int)column, y + (int)row, color);
        }
        cursor += 6;
        if (cursor >= (int)canvas->width) break;
    }
    return 0;
}

static void draw_text(cam_canvas *canvas, int x, int y, const char *text, uint32_t color)
{
    for (size_t index = 0; text[index] != '\0' && index < 40; index++) {
        int xpos = x + (int)index * 6;
        for (unsigned int column = 0; column < 6; column++) {
            uint8_t bits = column < 5 ? glyph_column(text[index], column) : 0;
            for (unsigned int row = 0; row < 8; row++) {
                put_pixel(canvas, xpos + (int)column, y + (int)row,
                    (bits & (1U << row)) != 0 ? color : CAM_BLACK);
            }
        }
    }
}

static unsigned int clamp_rgb(int value)
{
    return value < 0 ? 0U : (value > 255 ? 255U : (unsigned int)value);
}

static uint32_t nv21_pixel(const cam_image *image, uint32_t x, uint32_t y)
{
    int luminance = image->y[(size_t)y * image->stride_y + x] - 16;
    size_t offset = (size_t)(y / 2) * image->stride_vu + (x & ~1U);
    int v = image->vu[offset] - 128;
    int u = image->vu[offset + 1] - 128;
    if (luminance < 0) {
        luminance = 0;
    }
    unsigned int r = clamp_rgb((298 * luminance + 409 * v + 128) >> 8);
    unsigned int g = clamp_rgb((298 * luminance - 100 * u - 208 * v + 128) >> 8);
    unsigned int b = clamp_rgb((298 * luminance + 516 * u + 128) >> 8);
    return (r << 16) | (g << 8) | b;
}

static void draw_object(cam_canvas *canvas, const cam_image *image,
    const cam_viewport *view, const cam_object *object)
{
    cam_viewport box;
    char label[40];
    static const uint32_t colors[] = {0x00ffff00U, 0x0000ff00U, 0x0000ffffU, 0x00ff80ffU};
    if (!isfinite(object->confidence) || object->confidence < 0 || object->confidence > 1 ||
        cam_map_box(object, image, view, &box) != 0) {
        return;
    }
    int right=box.x+box.width,bottom=box.y+box.height;
    if(box.x>=(int)canvas->width||box.y>=(int)canvas->height||right<=0||bottom<=0)return;
    if(box.x<0)box.x=0;
    if(box.y<0)box.y=0;
    if(right>(int)canvas->width)right=(int)canvas->width;
    if(bottom>(int)canvas->height)bottom=(int)canvas->height;
    box.width=right-box.x;box.height=bottom-box.y;
    uint32_t color = colors[(unsigned int)object->type % 4U];
    for (int thickness = 0; thickness < 2; thickness++) {
        for (int x = box.x; x < box.x + box.width; x++) {
            put_pixel(canvas, x, box.y + thickness, color);
            put_pixel(canvas, x, box.y + box.height - 1 - thickness, color);
        }
        for (int y = box.y; y < box.y + box.height; y++) {
            put_pixel(canvas, box.x + thickness, y, color);
            put_pixel(canvas, box.x + box.width - 1 - thickness, y, color);
        }
    }
    int ret = snprintf(label, sizeof(label), "%s %.0f%%", cam_class_name(object->type),
        object->confidence * 100.0);
    if (ret < 0 || (size_t)ret >= sizeof(label)) {
        return;
    }
    int text_x = box.x;
    int text_width = ret * 6;
    if (text_x + text_width > (int)canvas->width) {
        text_x = (int)canvas->width - text_width;
    }
    if (text_x < 0) {
        text_x = 0;
    }
    int text_y = box.y >= 9 ? box.y - 9 : box.y + 2;
    draw_text(canvas, text_x, text_y, label, color);
}

int cam_render_ui(cam_canvas *canvas, const cam_image *image, const cam_result *result, const cam_stats *stats)
{
    cam_viewport view;
    char status[64];
    double fps = stats != NULL ? stats->processing_fps : 0;
    if (canvas == NULL || canvas->pixels == NULL || result == NULL ||
        result->count > CAM_MAX_OBJECTS || !cam_image_valid(image) ||
        canvas->stride < canvas->width || canvas->height == 0 ||
        canvas->stride > SIZE_MAX / canvas->height ||
        canvas->capacity < (size_t)canvas->stride * canvas->height ||
        cam_view_fill(image->width, image->height, canvas->width, canvas->height, &view) != 0) {
        return -1;
    }
    for (uint32_t y=0;y<canvas->height;y++) {
        uint32_t sy=(uint64_t)((int)y-view.y)*image->height/view.height;
        for(uint32_t x=0;x<canvas->width;x++) {
            uint32_t sx=(uint64_t)((int)x-view.x)*image->width/view.width;
            canvas->pixels[(size_t)y*canvas->stride+x]=nv21_pixel(image,sx,sy);
        }
    }
    for (size_t i = 0; i < result->count; i++) {
        draw_object(canvas, image, &view, &result->objects[i]);
    }
    if (!isfinite(fps) || fps < 0 || fps > 999) {
        fps = 0;
    }
    if (stats == NULL || !stats->show_overlay) return 0;
    int ret = snprintf(status, sizeof(status), "VIEW %.1fFPS OBJ %zu SAVED %u", fps,
        result->count, stats != NULL ? stats->saved : 0);
    if (ret < 0 || (size_t)ret >= sizeof(status)) return -1;
    draw_text(canvas, 3, (int)canvas->height - 13, status, CAM_WHITE);
    if (stats != NULL && stats->sensor_width != 0) {
        if (stats->sensor_fps > 0 && isfinite(stats->sensor_fps))
            snprintf(status, sizeof(status), "CAM %uX%u %.1fFPS", stats->sensor_width,
                stats->sensor_height, stats->sensor_fps);
        else
            snprintf(status, sizeof(status), "CAM %uX%u FPS NA", stats->sensor_width, stats->sensor_height);
    } else {
        snprintf(status, sizeof(status), "FILE %uX%u", image->width, image->height);
    }
    draw_text(canvas, 3, 5, status, CAM_WHITE);
    snprintf(status, sizeof(status), "AI %s INPUT %uX%u", stats->ai_enabled ? "ON" : "OFF", image->width, image->height);
    draw_text(canvas, 3, 21, status, CAM_WHITE);
    return 0;
}

int cam_render(cam_canvas *canvas, const cam_image *image, const cam_result *result, double fps)
{
    cam_stats stats = {.processing_fps = fps};
    return cam_render_ui(canvas, image, result, &stats);
}

int cam_snapshot_hit(int x, int y, uint32_t width)
{
    return width >= 100 && x >= (int)width - 88 && x < (int)width - 3 && y >= 4 && y < 35;
}

int cam_rotate_to_panel(const cam_canvas *source, cam_canvas *panel, int rotation)
{
    if (source == NULL || panel == NULL || source->pixels == NULL || panel->pixels == NULL ||
        source->stride < source->width || panel->stride < panel->width ||
        source->height == 0 || panel->height == 0 ||
        source->stride > SIZE_MAX / source->height || panel->stride > SIZE_MAX / panel->height ||
        source->capacity < (size_t)source->stride * source->height ||
        panel->capacity < (size_t)panel->stride * panel->height) return -1;
    if (rotation == 0) {
        if (source->width != panel->width || source->height != panel->height) return -1;
    } else if (rotation == 90 || rotation == 270) {
        if (source->width != panel->height || source->height != panel->width) return -1;
    } else return -1;
    for (uint32_t y = 0; y < source->height; ++y) {
        for (uint32_t x = 0; x < source->width; ++x) {
            uint32_t px = x, py = y;
            if (rotation == 90) { px = panel->width - 1 - y; py = x; }
            if (rotation == 270) { px = y; py = panel->height - 1 - x; }
            panel->pixels[(size_t)py * panel->stride + px] = source->pixels[(size_t)y * source->stride + x];
        }
    }
    return 0;
}
