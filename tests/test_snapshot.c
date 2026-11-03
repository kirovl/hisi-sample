/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    assert(argc == 2);
    uint32_t pixels[8] = {0xff0000, 0x00ff00, 0x0000ff, 0x123456,
                          0xffffff, 0x000000, 0xabcdef, 0x123456};
    cam_canvas canvas = {pixels,3,2,4,8};
    assert(cam_save_png(argv[1], &canvas) == 0);
    canvas.capacity = 6;
    assert(cam_save_png(argv[1], &canvas) == -1);
    puts("PNG writer: stride and capacity checks passed");
    return 0;
}
