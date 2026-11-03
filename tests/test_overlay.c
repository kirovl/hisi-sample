/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    uint8_t y[16 * 8];
    uint8_t vu[16 * 4];
    memset(y, 110, sizeof(y));
    memset(vu, 128, sizeof(vu));
    cam_image image = {16, 8, 16, 16, y, vu};
    uint8_t small_y[8 * 4] = {0};
    uint8_t small_vu[8 * 2] = {0};
    cam_image small = {8, 4, 8, 8, small_y, small_vu};
    assert(cam_resize_nv21(&image, &small) == 0);
    assert(small.y[31] == 110 && small.vu[15] == 128);
    cam_viewport view;
    assert(cam_view_fit(16, 8, 240, 320, &view) == 0);
    assert(view.x == 0 && view.y == 110 && view.width == 240 && view.height == 120);
    cam_object object = {1, .9f, -2, -2, 20, 12};
    cam_viewport box;
    assert(cam_map_box(&object, &image, &view, &box) == 0);
    assert(box.x == 0 && box.y == 110 && box.width == 240 && box.height == 120);
    cam_viewport full;
    assert(cam_view_fill(768,432,320,240,&full)==0);
    assert(full.x==-53&&full.y==0&&full.width==427&&full.height==240);
    assert(cam_view_fill(16,8,240,320,&full)==0&&full.x==-200&&full.width==640);
    assert(cam_view_fill(8,16,320,240,&full)==0&&full.y==-200&&full.height==640);
    assert(cam_view_fill(0,8,320,240,&full)==-1);
    object.x = INT_MAX;
    object.width = INT_MAX;
    assert(cam_map_box(&object, &image, &view, &box) == -1);
    object.x = INT_MIN;
    object.width = INT_MAX;
    assert(cam_map_box(&object, &image, &view, &box) == -1);
    size_t count = 240 * 320;
    uint32_t *pixels = calloc(count + 2, sizeof(*pixels));
    assert(pixels != NULL);
    pixels[0] = 0x12345678;
    pixels[count + 1] = 0xabcdef01;
    cam_canvas canvas = {pixels + 1, 240, 320, 240, count};
    cam_result results = {0};
    /* Every pixel, including corners, contains video with overlays disabled. */
    assert(cam_render(&canvas,&image,&results,0)==0);
    for(size_t i=0;i<count;i++)assert(canvas.pixels[i]!=0);
    uint32_t background=canvas.pixels[0];
    results.count=1;results.objects[0]=(cam_object){1,.9f,0,0,1,8};
    assert(cam_render(&canvas,&image,&results,0)==0);
    for(size_t i=0;i<count;i++)assert(canvas.pixels[i]==background); /* Cropped object has no ghost label. */
    results.objects[0]=(cam_object){1,.9f,5,0,1,8};
    assert(cam_render(&canvas,&image,&results,0)==0);
    assert(canvas.pixels[0]!=background); /* Same crop mapping puts x=5 at panel edge. */
    results.count = 3;
    results.objects[0] = (cam_object){1, .85f, 0, 0, 4, 4};
    results.objects[1] = (cam_object){3, .75f, 6, 2, 4, 4};
    results.objects[2] = (cam_object){11, .8f, 12, 4, 4, 4};
    assert(cam_render(&canvas, &image, &results, 5) == 0);
    assert(pixels[0] == 0x12345678 && pixels[count + 1] == 0xabcdef01);
    assert(canvas.pixels[110 * 240] != 0);
    canvas.capacity--;
    assert(cam_render(&canvas, &image, &results, 5) == -1);
    canvas.capacity++;
    results.count = CAM_MAX_OBJECTS + 1;
    assert(cam_render(&canvas, &image, &results, 5) == -1);
    image.width = 15;
    assert(cam_resize_nv21(&image, &small) == -1);
    uint32_t landscape_pixels[6] = {1,2,3,4,5,6}, portrait_pixels[6] = {0};
    cam_canvas landscape = {landscape_pixels, 3, 2, 3, 6};
    cam_canvas portrait = {portrait_pixels, 2, 3, 2, 6};
    assert(cam_rotate_to_panel(&landscape, &portrait, 90) == 0);
    const uint32_t expected90[6] = {4,1,5,2,6,3};
    assert(memcmp(portrait_pixels, expected90, sizeof(expected90)) == 0);
    assert(cam_rotate_to_panel(&landscape, &portrait, 270) == 0);
    const uint32_t expected270[6] = {3,6,2,5,1,4};
    assert(memcmp(portrait_pixels, expected270, sizeof(expected270)) == 0);
    assert(cam_snapshot_hit(250, 15, 320) && !cam_snapshot_hit(100, 15, 320));
    assert(!cam_snapshot_hit(250, 100, 320));
    free(pixels);
    puts("overlay tests passed: NV21 scaling, full-screen crop, box mapping, clipping, multiple objects, bounds");
    return 0;
}
