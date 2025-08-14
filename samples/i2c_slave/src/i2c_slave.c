/*
 * Copyright (c) 2025 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/random/random.h>
#include <zephyr/devicetree.h>

#include "i2c_slave.h"

LOG_MODULE_REGISTER(i2c_slave, LOG_LEVEL_INF);

#define I2C_TARGET_NODE DT_NODELABEL(i2c1)
#define I2C_TARGET_ADDR 0x20

#define MEASUREMENT_DELAY_MS 500
#define LOCK_TIMEOUT_MS      5000 // 5 seconds before locking again

const struct device *i2c_target_dev = DEVICE_DT_GET(I2C_TARGET_NODE);

// Mutex to protect registers
K_MUTEX_DEFINE(register_mutex);
#define LOCK_REGISTERS(timeout)   k_mutex_lock(&register_mutex, timeout)
#define UNLOCK_REGISTERS(timeout) k_mutex_unlock(&register_mutex)

// Fake hardware register contents
static uint8_t registers[REG_COUNT] = {
    0x42,          // WHO_AM_I
    0x00,          // CTRL1
    STATUS_LOCKED, // STATUS: locked, no data-ready
    0xFF,          // TEMP_L (locked => FF)
    0xFF,          // TEMP_H
    0xFF,          // HUMID_L
    0xFF,          // HUMID_H
    0x00,          // CONFIG
    0x00,          // padding
    0x00,          // SESSION_CODE (filled at init)
    0x00,          // UNLOCK
    LOCKED         // LOCK_STATUS (locked)
};

// Read-only register mask
static bool reg_readonly[REG_COUNT] = {
    true,  // WHO_AM_I
    false, // CTRL1
    true,  // STATUS
    true,  // TEMP_L
    true,  // TEMP_H
    true,  // HUMID_L
    true,  // HUMID_H
    false, // CONFIG
    false, // padding
    true,  // SESSION_CODE
    false, // UNLOCK
    true   // LOCK_STATUS
};

// Internal state
static uint8_t current_reg;
static bool reg_addr_received;

// Measurement state
static bool measurement_in_progress;
static int64_t measurement_start_time_ms;

// Unlock timeout tracking
static int64_t unlock_time_ms;

// Forward declarations
static void start_measurement(void);
static void complete_measurement(void);
static void lock_device(void);
static void unlock_device(void);

// Locking helper
static void lock_device(void)
{
    LOCK_REGISTERS(K_FOREVER);

    registers[REG_LOCK_STATUS] = LOCKED;

    // Mark status as locked
    registers[REG_STATUS] |= STATUS_LOCKED;

    // Clear data-ready bit
    registers[REG_STATUS] &= ~STATUS_DATA_READY;

    // Clear TEMP/HUMID when locked
    registers[REG_TEMP_L] = 0xFF;
    registers[REG_TEMP_H] = 0xFF;
    registers[REG_HUMID_L] = 0xFF;
    registers[REG_HUMID_H] = 0xFF;
    measurement_in_progress = false;
    LOG_INF("Device locked");

    UNLOCK_REGISTERS();
}

static void unlock_device(void)
{
    LOCK_REGISTERS(K_FOREVER);

    registers[REG_LOCK_STATUS] = UNLOCKED;

    // Clear locked flag in STATUS
    registers[REG_STATUS] &= ~STATUS_LOCKED;

    unlock_time_ms = k_uptime_get();
    LOG_INF("Device unlocked");

    UNLOCK_REGISTERS();
}

// Periodic lock timeout check
static void lock_timeout_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    if (registers[REG_LOCK_STATUS] == UNLOCKED) {
        if ((k_uptime_get() - unlock_time_ms) > LOCK_TIMEOUT_MS) {
            lock_device();
        }
    }
}

K_TIMER_DEFINE(lock_timer, lock_timeout_handler, NULL);

// start_measurement: kick off a measurement (non-blocking)
static void start_measurement(void)
{
    if (registers[REG_LOCK_STATUS] == LOCKED) {
        LOG_WRN("Attempted measurement while locked");
        return;
    }
    if (measurement_in_progress) {
        LOG_WRN("Measurement already in progress");
        return;
    }
    measurement_in_progress = true;
    measurement_start_time_ms = k_uptime_get();

    // clear data ready bit
    registers[REG_STATUS] &= ~STATUS_DATA_READY;
    LOG_INF("Measurement started");
}

// complete_measurement: finish measurement, write temp/humid, set data-ready
static void complete_measurement(void)
{
    // Generate fake temperature/humidity
    uint16_t temp = 2000 + (sys_rand32_get() % 1000);  // 20.00°C base
    uint16_t humid = 5000 + (sys_rand32_get() % 2000); // 50.00% base

    LOCK_REGISTERS(K_FOREVER);

    registers[REG_TEMP_L] = (uint8_t)(temp & 0xFF);
    registers[REG_TEMP_H] = (uint8_t)((temp >> 8) & 0xFF);
    registers[REG_HUMID_L] = (uint8_t)(humid & 0xFF);
    registers[REG_HUMID_H] = (uint8_t)((humid >> 8) & 0xFF);

    registers[REG_STATUS] |= STATUS_DATA_READY;

    UNLOCK_REGISTERS();

    measurement_in_progress = false;
    LOG_INF("Measurement complete: temp=%u (x0.01), humid=%u (x0.01)", temp,
            humid);
}

// I2C Callbacks

// Callback: master starts a write
static int target_write_requested(struct i2c_target_config *target_config)
{
    LOG_INF("Write requested");
    reg_addr_received = false;
    return 0;
}

// Callback: master sends data byte
static int target_write_received(struct i2c_target_config *target_config,
                                 uint8_t val)
{
    if (!reg_addr_received) {
        // First byte is register address
        current_reg = val % REG_COUNT;
        reg_addr_received = true;
        LOG_INF("Register address set to 0x%02X", current_reg);
    } else {
        LOG_INF("Want to write to: 0x%02X", current_reg);
        // Subsequent bytes are data
        if (reg_readonly[current_reg]) {
            LOG_WRN("Write attempt to read-only reg 0x%02X", current_reg);
        } else {
            LOG_INF("Write reg[0x%02X] = 0x%02X", current_reg, val);
            registers[current_reg] = val;

            // Special: CTRL1 start measurement (bit0)
            if (current_reg == REG_CTRL1 && (val & 0x01)) {
                start_measurement();
            }

            // Special: unlock logic
            if (current_reg == REG_UNLOCK) {
                if (val == registers[REG_SESSION_CODE]) {
                    unlock_device();
                } else {
                    LOG_WRN("Incorrect unlock code: 0x%02X", val);
                }
            }
        }
        current_reg = (current_reg + 1) % REG_COUNT; // Auto-increment
    }
    return 0;
}

// Callback: master starts a read
static int target_read_requested(struct i2c_target_config *target_config,
                                 uint8_t *val)
{
    *val = registers[current_reg];
    LOG_INF("Read reg[0x%02X] = 0x%02X", current_reg, *val);
    return 0;
}

// Callback: master reads more data
static int target_read_processed(struct i2c_target_config *target_config,
                                 uint8_t *val)
{
    current_reg = (current_reg + 1) % REG_COUNT;
    *val = registers[current_reg];
    LOG_INF("Read reg[0x%02X] = 0x%02X", current_reg, *val);
    return 0;
}

// Callback: stop condition
static int target_stop(struct i2c_target_config *config)
{
    LOG_INF("Stop condition");
    return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
    .write_requested = target_write_requested,
    .read_requested = target_read_requested,
    .write_received = target_write_received,
    .read_processed = target_read_processed,
    .stop = target_stop,
};

static struct i2c_target_config target_config = {
    .address = I2C_TARGET_ADDR,
    .callbacks = &target_callbacks,
};

static int i2c_slave_init(void)
{
    // Generate random session code
    uint8_t code = (uint8_t)(sys_rand32_get() & 0xFF);
    registers[REG_SESSION_CODE] = code;

    // STATUS initial: locked
    registers[REG_STATUS] = STATUS_LOCKED;
    registers[REG_LOCK_STATUS] = LOCKED;

    int ret = i2c_target_register(i2c_target_dev, &target_config);
    if (ret) {
        LOG_ERR("Failed to register i2c target: %d", ret);
        return ret;
    }

    // Start lock timeout timer (tick every 1s)
    k_timer_start(&lock_timer, K_MSEC(1000), K_MSEC(1000));
    return 0;
}

int i2c_slave_main(void)
{
    i2c_slave_init();

    while (1) {
        uint32_t now = k_uptime_get();
        if (measurement_in_progress &&
            (now - measurement_start_time_ms) >= MEASUREMENT_DELAY_MS) {
            complete_measurement();
        }
        k_msleep(50);
    }
}

#define I2C_SLAVE_THREAD_STACKSIZE 1024
#define I2C_SLAVE_THREAD_PRIORITY  6

K_THREAD_DEFINE(i2c_slave_thread, I2C_SLAVE_THREAD_STACKSIZE, i2c_slave_main,
                NULL, NULL, NULL, I2C_SLAVE_THREAD_PRIORITY, 0, 0);
