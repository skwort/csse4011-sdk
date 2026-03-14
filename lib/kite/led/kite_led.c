/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <stdlib.h>

#include "../kite_shell.h"

#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_g =
    GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

static struct k_work_delayable blink_work;
static struct gpio_dt_spec const *blink_led;
static uint32_t blink_period_ms;
static bool blink_active;

static int led_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) ||
        !gpio_is_ready_dt(&led_b)) {
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void blink_work_handler(struct k_work *work)
{
    if (!blink_active || blink_led == NULL) {
        return;
    }

    gpio_pin_toggle_dt(blink_led);
    k_work_reschedule(&blink_work, K_MSEC(blink_period_ms));
}

static const struct gpio_dt_spec *color_to_led(const char *color)
{
    if (strcmp(color, "r") == 0 || strcmp(color, "red") == 0) {
        return &led_r;
    } else if (strcmp(color, "g") == 0 || strcmp(color, "green") == 0) {
        return &led_g;
    } else if (strcmp(color, "b") == 0 || strcmp(color, "blue") == 0) {
        return &led_b;
    }

    return NULL;
}

/* kite led set <r 0|1> <g 0|1> <b 0|1> */
static int cmd_led_set(const struct shell *sh, size_t argc, char **argv)
{
    static bool initialised;
    int ret;

    if (!initialised) {
        ret = led_init();
        if (ret < 0) {
            shell_error(sh, "LED init failed: %d", ret);
            return ret;
        }
        initialised = true;
    }

    uint32_t r = shell_strtoul(argv[1], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid red value");
        return ret;
    }

    uint32_t g = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid green value");
        return ret;
    }

    uint32_t b = shell_strtoul(argv[3], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid blue value");
        return ret;
    }

    gpio_pin_set_dt(&led_r, r != 0);
    gpio_pin_set_dt(&led_g, g != 0);
    gpio_pin_set_dt(&led_b, b != 0);

    shell_print(sh, "LED: R=%u G=%u B=%u", r, g, b);
    return 0;
}

/* kite led off */
static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
    if (blink_active) {
        blink_active = false;
        k_work_cancel_delayable(&blink_work);
    }

    gpio_pin_set_dt(&led_r, 0);
    gpio_pin_set_dt(&led_g, 0);
    gpio_pin_set_dt(&led_b, 0);

    shell_print(sh, "LEDs off");
    return 0;
}

/* kite led blink <color> <period_ms> */
static int cmd_led_blink(const struct shell *sh, size_t argc, char **argv)
{
    static bool work_initialised;
    int ret;

    const struct gpio_dt_spec *led = color_to_led(argv[1]);

    if (led == NULL) {
        shell_error(sh, "Unknown color '%s' (use r/g/b or red/green/blue)",
                    argv[1]);
        return -EINVAL;
    }

    uint32_t period = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0 || period == 0) {
        shell_error(sh, "Invalid period (must be > 0 ms)");
        return -EINVAL;
    }

    if (!work_initialised) {
        k_work_init_delayable(&blink_work, blink_work_handler);
        work_initialised = true;
    }

    /* Stop any active blink */
    if (blink_active) {
        blink_active = false;
        k_work_cancel_delayable(&blink_work);
        gpio_pin_set_dt(&led_r, 0);
        gpio_pin_set_dt(&led_g, 0);
        gpio_pin_set_dt(&led_b, 0);
    }

    blink_led = led;
    blink_period_ms = period;
    blink_active = true;

    k_work_reschedule(&blink_work, K_NO_WAIT);

    shell_print(sh, "Blinking %s at %u ms", argv[1], period);
    return 0;
}

/* Subcommands for 'kite led' */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_led_cmds,
    SHELL_CMD_ARG(set, NULL,
                  "Set RGB LED state\n"
                  "Usage: kite led set <r 0|1> <g 0|1> <b 0|1>",
                  cmd_led_set, 4, 0),
    SHELL_CMD(off, NULL, "Turn off all LEDs", cmd_led_off),
    SHELL_CMD_ARG(blink, NULL,
                  "Blink an LED\n"
                  "Usage: kite led blink <r|g|b> <period_ms>",
                  cmd_led_blink, 3, 0),
    SHELL_SUBCMD_SET_END);

KITE_CMD_ADD(led, &kite_led_cmds, "RGB LED commands", NULL);
