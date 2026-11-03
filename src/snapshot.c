/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "camera_ai_lcd.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static void be32(unsigned char *p, uint32_t value)
{
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t length)
{
    while (length--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static int chunk(FILE *file, const char type[4], const unsigned char *data, uint32_t length)
{
    unsigned char encoded[4]; be32(encoded, length);
    uint32_t crc = crc32_update(0xffffffffU, (const unsigned char *)type, 4);
    crc = crc32_update(crc, data, length) ^ 0xffffffffU;
    if (fwrite(encoded, 1, 4, file) != 4 || fwrite(type, 1, 4, file) != 4 ||
        (length != 0 && fwrite(data, 1, length, file) != length)) return -1;
    be32(encoded, crc); return fwrite(encoded, 1, 4, file) == 4 ? 0 : -1;
}
int cam_save_png(const char *path, const cam_canvas *canvas)
{
    if (path == NULL || canvas == NULL || canvas->pixels == NULL ||
        canvas->width == 0 || canvas->height == 0 || canvas->width > CAM_MAX_IMAGE_SIZE ||
        canvas->height > CAM_MAX_IMAGE_SIZE || canvas->stride < canvas->width ||
        canvas->stride > SIZE_MAX / canvas->height ||
        canvas->capacity < (size_t)canvas->stride * canvas->height) return -1;
    size_t row_size = (size_t)canvas->width * 3 + 1;
    size_t raw_size = row_size * canvas->height;
    size_t blocks = (raw_size + 65534) / 65535;
    size_t encoded_size = raw_size + 5 * blocks + 6;
    unsigned char *raw = malloc(raw_size), *encoded = malloc(encoded_size);
    if (raw == NULL || encoded == NULL) { free(raw); free(encoded); return -1; }
    for (uint32_t y = 0; y < canvas->height; ++y) {
        unsigned char *row = raw + (size_t)y * row_size; *row++ = 0;
        for (uint32_t x = 0; x < canvas->width; ++x) {
            uint32_t pixel = canvas->pixels[(size_t)y * canvas->stride + x];
            *row++ = pixel >> 16; *row++ = pixel >> 8; *row++ = pixel;
        }
    }
    size_t pos = 0, offset = 0;
    encoded[pos++] = 0x78; encoded[pos++] = 0x01;
    uint32_t a = 1, b = 0;
    while (offset < raw_size) {
        unsigned int length = raw_size - offset > 65535 ? 65535 : (unsigned int)(raw_size - offset);
        encoded[pos++] = offset + length == raw_size ? 1 : 0;
        encoded[pos++] = length; encoded[pos++] = length >> 8;
        encoded[pos++] = ~length; encoded[pos++] = (~length) >> 8;
        memcpy(encoded + pos, raw + offset, length); pos += length;
        for (unsigned int i = 0; i < length; ++i) { a = (a + raw[offset + i]) % 65521; b = (b + a) % 65521; }
        offset += length;
    }
    be32(encoded + pos, (b << 16) | a); pos += 4;
    char temporary[4096];
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
    int fd = n > 0 && (size_t)n < sizeof(temporary) ? mkstemp(temporary) : -1;
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    int rc = -1;
    if (file != NULL) {
        const unsigned char signature[8] = {137,80,78,71,13,10,26,10};
        unsigned char ihdr[13] = {0}; be32(ihdr, canvas->width); be32(ihdr + 4, canvas->height);
        ihdr[8] = 8; ihdr[9] = 2;
        int ok = fwrite(signature, 1, 8, file) == 8 && chunk(file, "IHDR", ihdr, 13) == 0 &&
            chunk(file, "IDAT", encoded, (uint32_t)pos) == 0 && chunk(file, "IEND", NULL, 0) == 0;
        if (fflush(file) != 0 || fsync(fd) != 0) ok = 0;
        if (fclose(file) != 0) ok = 0;
        if (ok && rename(temporary, path) == 0) rc = 0;
        if (rc != 0) unlink(temporary);
    } else if (fd >= 0) { close(fd); unlink(temporary); }
    free(raw); free(encoded); return rc;
}
int cam_save_capture(const char *directory, const cam_canvas *canvas, const cam_image *image,
    const cam_result *result, const cam_stats *stats, unsigned int sequence)
{
    if (directory == NULL || image == NULL || result == NULL || stats == NULL || result->count > CAM_MAX_OBJECTS) return -1;
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) return -1;
    struct timespec now; if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    char path[4096], json[4096];
    int n = snprintf(path, sizeof(path), "%s/shot-%ld-%ld-%06u.png", directory, (long)now.tv_sec, (long)getpid(), sequence);
    if (n < 0 || (size_t)n >= sizeof(path) || cam_save_png(path, canvas) != 0) return -1;
    n = snprintf(json, sizeof(json), "%.*s.json", (int)strlen(path) - 4, path);
    if (n < 0 || (size_t)n >= sizeof(json)) { unlink(path); return -1; }
    FILE *file = fopen(json, "w");
    if (file == NULL) { unlink(path); return -1; }
    fprintf(file, "{\n  \"source\":\"live-camera\",\"canvas\":[%u,%u],\"sensor\":[%u,%u],\n"
        "  \"sensor_fps\":%.2f,\"processing_fps\":%.2f,\"ai_input\":[%u,%u],\n  \"objects\":[",
        canvas->width, canvas->height, stats->sensor_width, stats->sensor_height,
        stats->sensor_fps, stats->processing_fps, image->width, image->height);
    for (size_t i = 0; i < result->count; ++i) {
        const cam_object *o = &result->objects[i];
        fprintf(file, "%s{\"class\":\"%s\",\"confidence\":%.3f,\"box\":[%d,%d,%d,%d]}",
            i ? "," : "", cam_class_name(o->type), o->confidence, o->x, o->y, o->width, o->height);
    }
    fprintf(file, "]\n}\n");
    int failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed) { unlink(json); unlink(path); return -1; }
    printf("SNAPSHOT saved=%s metadata=%s objects=%zu\n", path, json, result->count);
    return 0;
}
