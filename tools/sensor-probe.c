/* SPDX-License-Identifier: Apache-2.0 */
/* OS04D10 Sensor0 diagnostic: no sensor register writes or flash operations. */
#define _DEFAULT_SOURCE
#include "ot_mipi_rx.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int control(int fd, unsigned long request, void *value, const char *name)
{
    int rc = ioctl(fd, request, value);
    printf("%s: rc=%d errno=%d (%s)\n", name, rc, rc < 0 ? errno : 0,
           rc < 0 ? strerror(errno) : "OK");
    return rc;
}

int main(void)
{
    int rc = 1, clock_on = 0;
    int mipi = open("/dev/ot_mipi_rx", O_RDWR);
    int bus = -1;
    lane_divide_mode_t mode = LANE_DIVIDE_MODE_1;
    sns_clk_source_t clock = 0;
    sns_rst_source_t reset = 0;
    if (mipi < 0) { perror("open MIPI"); return 1; }
    if (control(mipi, OT_MIPI_SET_HS_MODE, &mode, "lane mode 2+2") < 0) goto done;
    if (control(mipi, OT_MIPI_ENABLE_SENSOR_CLOCK, &clock, "enable MCLK0") < 0) goto done;
    clock_on = 1;
    if (control(mipi, OT_MIPI_RESET_SENSOR, &reset, "assert reset0") < 0) goto done;
    usleep(20000);
    if (control(mipi, OT_MIPI_UNRESET_SENSOR, &reset, "release reset0") < 0) goto done;
    usleep(100000);
    bus = open("/dev/i2c-0", O_RDWR);
    if (bus < 0) { perror("open I2C0"); goto done; }
    /* The bytes are raw diagnostics, not a claimed chip-ID definition. */
    const unsigned char registers[] = {0x02, 0x03, 0x04, 0xfd};
    unsigned int passed = 0;
    for (unsigned int i = 0; i < sizeof(registers); ++i) {
        unsigned char reg = registers[i], data = 0;
        struct i2c_msg messages[2] = {
            {.addr = 0x3c, .flags = 0, .len = 1, .buf = &reg},
            {.addr = 0x3c, .flags = I2C_M_RD, .len = 1, .buf = &data}};
        struct i2c_rdwr_ioctl_data transfer = {.msgs = messages, .nmsgs = 2};
        int result = ioctl(bus, I2C_RDWR, &transfer);
        if (result == 2) {
            printf("I2C0 0x3c register 0x%02x = 0x%02x\n", reg, data);
            ++passed;
        } else {
            printf("I2C0 0x3c register 0x%02x: rc=%d %s\n", reg, result, strerror(errno));
        }
    }
    rc = passed == sizeof(registers) ? 0 : 1;
done:
    if (bus >= 0) close(bus);
    if (clock_on) {
        control(mipi, OT_MIPI_RESET_SENSOR, &reset, "restore reset0");
        control(mipi, OT_MIPI_DISABLE_SENSOR_CLOCK, &clock, "disable MCLK0");
    }
    close(mipi);
    return rc;
}
