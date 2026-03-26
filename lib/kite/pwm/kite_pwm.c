/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <stdlib.h>

#include "../kite_shell.h"

#define HAS_PWM_DEV  DT_NODE_EXISTS(DT_ALIAS(kite_pwm))
#define HAS_PWM_LEDS DT_NODE_EXISTS(DT_NODELABEL(kite_pwm_red))

#if HAS_PWM_LEDS
static const struct pwm_dt_spec pwm_red =
    PWM_DT_SPEC_GET(DT_NODELABEL(kite_pwm_red));
static const struct pwm_dt_spec pwm_green =
    PWM_DT_SPEC_GET(DT_NODELABEL(kite_pwm_green));
static const struct pwm_dt_spec pwm_blue =
    PWM_DT_SPEC_GET(DT_NODELABEL(kite_pwm_blue));
#endif

#if HAS_PWM_DEV
/* kite pwm set <channel> <period_us> <duty_us> */
static int cmd_pwm_set(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(kite_pwm));

    if (!device_is_ready(dev)) {
        shell_error(sh, "PWM device not ready");
        return -ENODEV;
    }

    int ret;
    uint32_t channel = shell_strtoul(argv[1], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid channel");
        return ret;
    }

    uint32_t period_us = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0 || period_us == 0) {
        shell_error(sh, "Invalid period (us)");
        return -EINVAL;
    }

    uint32_t duty_us = shell_strtoul(argv[3], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid duty cycle (us)");
        return ret;
    }

    if (duty_us > period_us) {
        shell_error(sh, "Duty cycle cannot exceed period");
        return -EINVAL;
    }

    ret = pwm_set(dev, channel, PWM_USEC(period_us), PWM_USEC(duty_us), 0);
    if (ret < 0) {
        shell_error(sh, "PWM set failed: %d", ret);
        return ret;
    }

    shell_print(sh, "PWM ch%u: period=%uus duty=%uus", channel, period_us,
                duty_us);
    return 0;
}

/* kite pwm stop <channel> */
static int cmd_pwm_stop(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(kite_pwm));

    if (!device_is_ready(dev)) {
        shell_error(sh, "PWM device not ready");
        return -ENODEV;
    }

    int ret;
    uint32_t channel = shell_strtoul(argv[1], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid channel");
        return ret;
    }

    ret = pwm_set(dev, channel, 0, 0, 0);
    if (ret < 0) {
        shell_error(sh, "PWM stop failed: %d", ret);
        return ret;
    }

    shell_print(sh, "PWM ch%u stopped", channel);
    return 0;
}
#endif /* HAS_PWM_DEV */

#if HAS_PWM_LEDS
/* kite pwm led <r 0-255> <g 0-255> <b 0-255> */
static int cmd_pwm_led(const struct shell *sh, size_t argc, char **argv)
{
    if (!pwm_is_ready_dt(&pwm_red) || !pwm_is_ready_dt(&pwm_green) ||
        !pwm_is_ready_dt(&pwm_blue)) {
        shell_error(sh, "PWM LED device not ready");
        return -ENODEV;
    }

    int ret;
    uint32_t r = shell_strtoul(argv[1], 0, &ret);

    if (ret < 0 || r > 255) {
        shell_error(sh, "Invalid red value (0-255)");
        return -EINVAL;
    }

    uint32_t g = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0 || g > 255) {
        shell_error(sh, "Invalid green value (0-255)");
        return -EINVAL;
    }

    uint32_t b = shell_strtoul(argv[3], 0, &ret);

    if (ret < 0 || b > 255) {
        shell_error(sh, "Invalid blue value (0-255)");
        return -EINVAL;
    }

    /* Scale 0-255 to 0-period for duty cycle */
    uint32_t duty_r = ((uint64_t)pwm_red.period * r) / 255;
    uint32_t duty_g = ((uint64_t)pwm_green.period * g) / 255;
    uint32_t duty_b = ((uint64_t)pwm_blue.period * b) / 255;

    ret = pwm_set_pulse_dt(&pwm_red, duty_r);
    if (ret < 0) {
        shell_error(sh, "Red PWM failed: %d", ret);
        return ret;
    }

    ret = pwm_set_pulse_dt(&pwm_green, duty_g);
    if (ret < 0) {
        shell_error(sh, "Green PWM failed: %d", ret);
        return ret;
    }

    ret = pwm_set_pulse_dt(&pwm_blue, duty_b);
    if (ret < 0) {
        shell_error(sh, "Blue PWM failed: %d", ret);
        return ret;
    }

    shell_print(sh, "LED: R=%u G=%u B=%u", r, g, b);
    return 0;
}
#endif /* HAS_PWM_LEDS */

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_pwm_cmds,
#if HAS_PWM_DEV
    SHELL_CMD_ARG(set, NULL,
                  "Set PWM output\n"
                  "Usage: kite pwm set <channel> <period_us> <duty_us>",
                  cmd_pwm_set, 4, 0),
    SHELL_CMD_ARG(stop, NULL,
                  "Stop PWM output on a channel\n"
                  "Usage: kite pwm stop <channel>",
                  cmd_pwm_stop, 2, 0),
#endif
#if HAS_PWM_LEDS
    SHELL_CMD_ARG(led, NULL,
                  "Set RGB LED brightness via PWM\n"
                  "Usage: kite pwm led <r 0-255> <g 0-255> <b 0-255>",
                  cmd_pwm_led, 4, 0),
#endif
    SHELL_SUBCMD_SET_END);
/* clang-format on */

KITE_CMD_ADD(pwm, &kite_pwm_cmds, "PWM commands", NULL);
