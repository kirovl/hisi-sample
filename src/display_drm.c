/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>

struct cam_display {
    int fd;
    int active;
    uint32_t connector;
    uint32_t crtc;
    uint32_t framebuffer;
    struct drm_mode_create_dumb dumb;
    drmModeModeInfo mode;
    drmModeCrtc *saved;
    uint32_t saved_connector;
    void *mapping;
    cam_canvas canvas;
    cam_canvas panel;
    int rotation;
};

void cam_display_close(struct cam_display *display)
{
    if (display == NULL) {
        return;
    }
    if (display->fd >= 0) {
        if (display->active) {
            if (display->saved != NULL && display->saved->mode_valid) {
                drmModeSetCrtc(display->fd, display->saved->crtc_id, display->saved->buffer_id,
                    display->saved->x, display->saved->y, &display->saved_connector, 1, &display->saved->mode);
            } else {
                drmModeSetCrtc(display->fd, display->crtc, 0, 0, 0, NULL, 0, NULL);
            }
        }
        if (display->mapping != NULL) {
            munmap(display->mapping, display->dumb.size);
        }
        if (display->framebuffer != 0) {
            drmModeRmFB(display->fd, display->framebuffer);
        }
        if (display->dumb.handle != 0) {
            struct drm_mode_destroy_dumb destroy = {0};
            destroy.handle = display->dumb.handle;
            drmIoctl(display->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        drmDropMaster(display->fd);
        close(display->fd);
    }
    if (display->saved != NULL) {
        drmModeFreeCrtc(display->saved);
    }
    free(display->canvas.pixels);
    free(display);
}

static int select_output(struct cam_display *display)
{
    drmModeRes *resources = drmModeGetResources(display->fd);
    if (resources == NULL) {
        return -1;
    }
    for (int i = 0; i < resources->count_connectors && display->crtc == 0; ++i) {
        drmModeConnector *connector = drmModeGetConnector(display->fd, resources->connectors[i]);
        if (connector == NULL) {
            continue;
        }
        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            for (int j = 0; j < connector->count_encoders && display->crtc == 0; ++j) {
                drmModeEncoder *encoder = drmModeGetEncoder(display->fd, connector->encoders[j]);
                if (encoder == NULL) {
                    continue;
                }
                for (int k = 0; k < resources->count_crtcs && k < 32; ++k) {
                    if ((encoder->possible_crtcs & (1U << k)) != 0) {
                        display->connector = connector->connector_id;
                        display->crtc = resources->crtcs[k];
                        display->mode = connector->modes[0];
                        break;
                    }
                }
                drmModeFreeEncoder(encoder);
            }
        }
        drmModeFreeConnector(connector);
    }
    if (display->crtc != 0) {
        display->saved = drmModeGetCrtc(display->fd, display->crtc);
        /* This module exclusively owns the single-output JD9853 card. */
        display->saved_connector = display->connector;
    }
    drmModeFreeResources(resources);
    return display->crtc != 0 ? 0 : -1;
}

int cam_display_open(struct cam_display **output, const char *device, int rotation)
{
    struct cam_display *display;
    struct drm_mode_map_dumb map = {0};
    if (output == NULL || device == NULL) {
        return -1;
    }
    *output = NULL;
    display = calloc(1, sizeof(*display));
    if (display == NULL) {
        return -1;
    }
    display->rotation = rotation;
    display->fd = open(device, O_RDWR | O_CLOEXEC);
    if (display->fd < 0) {
        fprintf(stderr, "Cannot open %s: %s. A DRM-enabled kernel and JD9853 driver are required.\n",
            device, strerror(errno));
        goto fail;
    }
    if (drmSetMaster(display->fd) != 0 || select_output(display) != 0) {
        fprintf(stderr, "Cannot acquire DRM output; stop any other display app.\n");
        goto fail;
    }
    if (display->mode.hdisplay != 240 || display->mode.vdisplay != 320) {
        fprintf(stderr, "Expected JD9853 240x320, got %ux%u\n", display->mode.hdisplay, display->mode.vdisplay);
        goto fail;
    }
    display->dumb.width = display->mode.hdisplay;
    display->dumb.height = display->mode.vdisplay;
    display->dumb.bpp = 32;
    if (drmIoctl(display->fd, DRM_IOCTL_MODE_CREATE_DUMB, &display->dumb) != 0) {
        perror("DRM create dumb buffer");
        goto fail;
    }
    if (display->dumb.pitch < display->dumb.width * 4U || (display->dumb.pitch & 3U) != 0 ||
        display->dumb.size < (uint64_t)display->dumb.pitch * display->dumb.height ||
        display->dumb.size > SIZE_MAX) {
        goto fail;
    }
    if (drmModeAddFB(display->fd, display->dumb.width, display->dumb.height, 24, 32,
        display->dumb.pitch, display->dumb.handle, &display->framebuffer) != 0) {
        perror("DRM add XRGB8888 framebuffer");
        goto fail;
    }
    map.handle = display->dumb.handle;
    if (drmIoctl(display->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        goto fail;
    }
    display->mapping = mmap(NULL, display->dumb.size, PROT_READ | PROT_WRITE,
        MAP_SHARED, display->fd, map.offset);
    if (display->mapping == MAP_FAILED) {
        display->mapping = NULL;
        goto fail;
    }
    display->panel = (cam_canvas){display->mapping, display->dumb.width,
        display->dumb.height, display->dumb.pitch / 4U, (size_t)display->dumb.size / 4U};
    if (rotation != 0 && rotation != 90 && rotation != 270) goto fail;
    display->canvas.width = rotation == 0 ? 240 : 320;
    display->canvas.height = rotation == 0 ? 320 : 240;
    display->canvas.stride = display->canvas.width;
    display->canvas.capacity = (size_t)display->canvas.width * display->canvas.height;
    display->canvas.pixels = calloc(display->canvas.capacity, sizeof(uint32_t));
    if (display->canvas.pixels == NULL) goto fail;
    memset(display->mapping, 0, display->dumb.size);
    if (drmModeSetCrtc(display->fd, display->crtc, display->framebuffer,
        0, 0, &display->connector, 1, &display->mode) != 0) {
        perror("DRM set display mode");
        goto fail;
    }
    display->active = 1;
    printf("DISPLAY %s JD9853 %ux%u XRGB8888\n", device, display->canvas.width, display->canvas.height);
    *output = display;
    return 0;
fail:
    cam_display_close(display);
    return -1;
}

cam_canvas *cam_display_canvas(struct cam_display *display)
{
    return display != NULL ? &display->canvas : NULL;
}

int cam_display_present(struct cam_display *display)
{
    if (display == NULL) {
        return -1;
    }
    if (cam_rotate_to_panel(&display->canvas, &display->panel, display->rotation) != 0) return -1;
    if (drmModeDirtyFB(display->fd, display->framebuffer, NULL, 0) != 0) {
        perror("DRM refresh framebuffer");
        return -1;
    }
    return 0;
}
