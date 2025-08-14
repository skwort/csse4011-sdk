/*
 * Copyright (c) 2025 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>

#include "i2c_slave.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define SLEEP_TIME_MS 1000

#define I2C_CONTROLLER_NODE DT_NODELABEL(i2c0)

static const struct device *i2c_dev = DEVICE_DT_GET(I2C_CONTROLLER_NODE);

// Helper to write a single register
static int i2c_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_write(i2c_dev, buf, sizeof(buf), I2C_SLAVE_ADDR);
}

// Helper to read a single register
static int i2c_read_reg(uint8_t reg, uint8_t *val)
{
    int ret = i2c_write(i2c_dev, &reg, 1, I2C_SLAVE_ADDR);
    if (ret) {
        return ret;
    }
    return i2c_read(i2c_dev, val, 1, I2C_SLAVE_ADDR);
}

// Helper to read two bytes and combine into uint16_t
static int i2c_read_u16(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    int ret = i2c_write(i2c_dev, &reg, 1, I2C_SLAVE_ADDR);
    if (ret) {
        return ret;
    }
    ret = i2c_read(i2c_dev, buf, sizeof(buf), I2C_SLAVE_ADDR);
    if (ret) {
        return ret;
    }
    *out = ((uint16_t)buf[1] << 8) | buf[0];
    return 0;
}

int main(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return 1;
    }

    // Let the slave start
    k_sleep(K_MSEC(100));

    uint8_t val;
    uint16_t temp, humid;
    int ret;

    // 1. Read WHO_AM_I
    ret = i2c_read_reg(REG_WHO_AM_I, &val);
    if (ret) {
        LOG_ERR("WHO_AM_I read failed: %d", ret);
        return 1;
    }
    LOG_INF("WHO_AM_I = 0x%02X", val);

    // 2. Attempt to start measurement while locked
    LOG_INF("Trying measurement while locked...");
    ret = i2c_write_reg(REG_CTRL1, 0x01);
    if (ret) {
        LOG_ERR("Write failed: %d", ret);
    }

    // 3. Read session code and unlock
    uint8_t session_code;
    i2c_read_reg(REG_SESSION_CODE, &session_code);
    LOG_INF("Session code: 0x%02X", session_code);

    LOG_INF("Unlocking device...");
    i2c_write_reg(REG_UNLOCK, session_code);

    // 4. Start measurement
    LOG_INF("Starting measurement...");
    i2c_write_reg(REG_CTRL1, 0x01);

    // 5. Poll until data ready
    while (1) {
        i2c_read_reg(REG_STATUS, &val);
        if (val & STATUS_DATA_READY) {
            break;
        }
        k_msleep(100);
    }
    LOG_INF("Data ready!");

    // 6. Read temp/humid
    i2c_read_u16(REG_TEMP_L, &temp);
    i2c_read_u16(REG_HUMID_H, &humid);
    LOG_INF("Temperature: %u (x0.01 °C)", temp);
    LOG_INF("Humidity: %u (x0.01 %%)", humid);

    // 7. Wait for auto-lock
    LOG_INF("Waiting for auto-lock...");
    while (1) {
        i2c_read_reg(REG_LOCK_STATUS, &val);
        if (val == LOCKED) {
            LOG_INF("Device locked again.");
            break;
        }
        k_msleep(500);
    }

    return 0;
}
