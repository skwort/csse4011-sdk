/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include "../kite_shell.h"

static const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));

static void print_sensor_val(const struct shell *sh, const char *label,
                             struct sensor_value *val)
{
    bool neg = (val->val1 < 0) || (val->val2 < 0);
    int32_t abs_val1 = val->val1 < 0 ? -val->val1 : val->val1;
    int32_t abs_val2 = val->val2 < 0 ? -val->val2 : val->val2;

    shell_print(sh, "  %s: %s%d.%06d", label, neg ? "-" : "", abs_val1,
                abs_val2);
}

/* kite imu read */
static int cmd_imu_read(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(imu_dev)) {
        shell_error(sh, "IMU device not ready");
        return -ENODEV;
    }

    int ret = sensor_sample_fetch(imu_dev);

    if (ret < 0) {
        shell_error(sh, "Fetch failed: %d", ret);
        return ret;
    }

    struct sensor_value accel[3];
    struct sensor_value gyro[3];

    ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret < 0) {
        shell_error(sh, "Accel get failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (ret < 0) {
        shell_error(sh, "Gyro get failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Accel (m/s^2):");
    print_sensor_val(sh, "X", &accel[0]);
    print_sensor_val(sh, "Y", &accel[1]);
    print_sensor_val(sh, "Z", &accel[2]);

    shell_print(sh, "Gyro (rad/s):");
    print_sensor_val(sh, "X", &gyro[0]);
    print_sensor_val(sh, "Y", &gyro[1]);
    print_sensor_val(sh, "Z", &gyro[2]);

    return 0;
}

/* kite imu accel */
static int cmd_imu_accel(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(imu_dev)) {
        shell_error(sh, "IMU device not ready");
        return -ENODEV;
    }

    int ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ);

    if (ret < 0) {
        shell_error(sh, "Fetch failed: %d", ret);
        return ret;
    }

    struct sensor_value accel[3];

    ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret < 0) {
        shell_error(sh, "Get failed: %d", ret);
        return ret;
    }

    print_sensor_val(sh, "X", &accel[0]);
    print_sensor_val(sh, "Y", &accel[1]);
    print_sensor_val(sh, "Z", &accel[2]);

    return 0;
}

/* kite imu gyro */
static int cmd_imu_gyro(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(imu_dev)) {
        shell_error(sh, "IMU device not ready");
        return -ENODEV;
    }

    int ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_GYRO_XYZ);

    if (ret < 0) {
        shell_error(sh, "Fetch failed: %d", ret);
        return ret;
    }

    struct sensor_value gyro[3];

    ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (ret < 0) {
        shell_error(sh, "Get failed: %d", ret);
        return ret;
    }

    print_sensor_val(sh, "X", &gyro[0]);
    print_sensor_val(sh, "Y", &gyro[1]);
    print_sensor_val(sh, "Z", &gyro[2]);

    return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_imu_cmds,
    SHELL_CMD_ARG(read, NULL,
                  "Read accelerometer and gyroscope\n"
                  "Usage: kite imu read",
                  cmd_imu_read, 1, 0),
    SHELL_CMD_ARG(accel, NULL,
                  "Read accelerometer only (m/s^2)\n"
                  "Usage: kite imu accel",
                  cmd_imu_accel, 1, 0),
    SHELL_CMD_ARG(gyro, NULL,
                  "Read gyroscope only (rad/s)\n"
                  "Usage: kite imu gyro",
                  cmd_imu_gyro, 1, 0),
    SHELL_SUBCMD_SET_END);
/* clang-format on */

KITE_CMD_ADD(imu, &kite_imu_cmds, "IMU commands", NULL);
