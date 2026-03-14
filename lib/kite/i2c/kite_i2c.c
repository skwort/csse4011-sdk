/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <stdlib.h>

#include "../kite_shell.h"

#define I2C_SCAN_FIRST 0x03
#define I2C_SCAN_LAST  0x77

static const struct device *i2c_dev = DEVICE_DT_GET(DT_ALIAS(kite_i2c));

/* kite i2c scan */
static int cmd_i2c_scan(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(i2c_dev)) {
        shell_error(sh, "I2C device not ready");
        return -ENODEV;
    }

    uint8_t cnt = 0;

    shell_print(sh, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    for (uint8_t row = 0; row <= 0x70; row += 16) {
        char line[64];
        int pos = snprintf(line, sizeof(line), "%02x: ", row);

        for (uint8_t col = 0; col < 16; col++) {
            uint8_t addr = row + col;

            if (addr < I2C_SCAN_FIRST || addr > I2C_SCAN_LAST) {
                pos += snprintf(&line[pos], sizeof(line) - pos, "   ");
                continue;
            }

            struct i2c_msg msg = {
                .buf = NULL,
                .len = 0,
                .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
            };
            int ret = i2c_transfer(i2c_dev, &msg, 1, addr);

            if (ret == 0) {
                pos += snprintf(&line[pos], sizeof(line) - pos, "%02x ", addr);
                cnt++;
            } else {
                pos += snprintf(&line[pos], sizeof(line) - pos, "-- ");
            }
        }
        shell_print(sh, "%s", line);
    }

    shell_print(sh, "%u device(s) found", cnt);
    return 0;
}

/* kite i2c read <addr> <len> */
static int cmd_i2c_read(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(i2c_dev)) {
        shell_error(sh, "I2C device not ready");
        return -ENODEV;
    }

    int ret;
    uint32_t addr = shell_strtoul(argv[1], 16, &ret);

    if (ret < 0 || addr > 0x7F) {
        shell_error(sh, "Invalid address '%s' (use hex 00-7F)", argv[1]);
        return -EINVAL;
    }

    uint32_t len = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0 || len == 0 || len > 256) {
        shell_error(sh, "Invalid length (1-256)");
        return -EINVAL;
    }

    uint8_t buf[256];

    ret = i2c_read(i2c_dev, buf, len, (uint16_t)addr);
    if (ret < 0) {
        shell_error(sh, "Read failed: %d", ret);
        return ret;
    }

    for (uint32_t i = 0; i < len; i += 16) {
        char line[64];
        int pos = snprintf(line, sizeof(line), "%02x: ", (unsigned int)i);

        for (uint32_t j = i; j < i + 16 && j < len; j++) {
            pos += snprintf(&line[pos], sizeof(line) - pos, "%02x ", buf[j]);
        }
        shell_print(sh, "%s", line);
    }

    return 0;
}

/* kite i2c write <addr> <data...> */
static int cmd_i2c_write(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(i2c_dev)) {
        shell_error(sh, "I2C device not ready");
        return -ENODEV;
    }

    int ret;
    uint32_t addr = shell_strtoul(argv[1], 16, &ret);

    if (ret < 0 || addr > 0x7F) {
        shell_error(sh, "Invalid address '%s' (use hex 00-7F)", argv[1]);
        return -EINVAL;
    }

    uint32_t num_bytes = argc - 2;

    if (num_bytes > 256) {
        shell_error(sh, "Too many bytes (max 256)");
        return -EINVAL;
    }

    uint8_t buf[256];

    for (uint32_t i = 0; i < num_bytes; i++) {
        uint32_t val = shell_strtoul(argv[2 + i], 16, &ret);

        if (ret < 0 || val > 0xFF) {
            shell_error(sh, "Invalid byte '%s' (use hex 00-FF)", argv[2 + i]);
            return -EINVAL;
        }
        buf[i] = (uint8_t)val;
    }

    ret = i2c_write(i2c_dev, buf, num_bytes, (uint16_t)addr);
    if (ret < 0) {
        shell_error(sh, "Write failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Wrote %u byte(s) to 0x%02x", num_bytes, addr);
    return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_i2c_cmds,
    SHELL_CMD_ARG(scan, NULL,
                  "Scan I2C bus for devices\n"
                  "Usage: kite i2c scan",
                  cmd_i2c_scan, 1, 0),
    SHELL_CMD_ARG(read, NULL,
                  "Read bytes from I2C device\n"
                  "Usage: kite i2c read <addr> <len>\n"
                  "  addr - device address in hex (00-7F)\n"
                  "  len  - number of bytes to read (1-256)",
                  cmd_i2c_read, 3, 0),
    SHELL_CMD_ARG(write, NULL,
                  "Write bytes to I2C device\n"
                  "Usage: kite i2c write <addr> <data...>\n"
                  "  addr - device address in hex (00-7F)\n"
                  "  data - hex bytes to write (00-FF)",
                  cmd_i2c_write, 3, 254),
    SHELL_SUBCMD_SET_END);
/* clang-format on */

KITE_CMD_ADD(i2c, &kite_i2c_cmds, "I2C commands", NULL);
