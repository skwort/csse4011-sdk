/*
 * Copyright (c) 2025 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _I2C_SLAVE_H_
#define _I2C_SLAVE_H_

#define I2C_SLAVE_ADDR 0x20

// Register addresses
enum {
    REG_WHO_AM_I = 0x00,
    REG_CTRL1 = 0x01,
    REG_STATUS = 0x02,
    REG_TEMP_L = 0x03,
    REG_TEMP_H = 0x04,
    REG_HUMID_H = 0x05,
    REG_HUMID_L = 0x06,
    REG_CONFIG = 0x07,
    REG_PADDING = 0x08,
    REG_SESSION_CODE = 0x09,
    REG_UNLOCK = 0x0A,
    REG_LOCK_STATUS = 0x0B,
    REG_COUNT = 0x0C
};

// STATUS bits
#define STATUS_DATA_READY (1 << 0)
#define STATUS_LOCKED     (1 << 1)

// LOCK_STATUS values
#define LOCKED   0x01
#define UNLOCKED 0x00

#endif // _I2C_SLAVE_H_
