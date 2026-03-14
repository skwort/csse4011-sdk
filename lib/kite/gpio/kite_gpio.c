/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>

#include "../kite_shell.h"

/*
 * XIAO BLE connector pin mapping (D0-D10).
 *
 * Pins are resolved via devicetree aliases (xiao-d0 .. xiao-d10).
 * The board overlay must define these aliases pointing to nodes with
 * gpios properties referencing the xiao_d connector, e.g.:
 *
 *   aliases { xiao-d0 = &xiao_d0; };
 *   xiao_gpio_pins {
 *       compatible = "gpio-keys";
 *       xiao_d0: d0 { gpios = <&xiao_d 0 0>; };
 *   };
 */

#define XIAO_PIN_SPEC(n) [n] = GPIO_DT_SPEC_GET(DT_ALIAS(xiao_d##n), gpios),

/* clang-format off */
static const struct gpio_dt_spec xiao_pins[] = {
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d0))
	XIAO_PIN_SPEC(0)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d1))
	XIAO_PIN_SPEC(1)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d2))
	XIAO_PIN_SPEC(2)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d3))
	XIAO_PIN_SPEC(3)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d4))
	XIAO_PIN_SPEC(4)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d5))
	XIAO_PIN_SPEC(5)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d6))
	XIAO_PIN_SPEC(6)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d7))
	XIAO_PIN_SPEC(7)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d8))
	XIAO_PIN_SPEC(8)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d9))
	XIAO_PIN_SPEC(9)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d10))
	XIAO_PIN_SPEC(10)
#endif
};
/* clang-format on */

#define NUM_XIAO_PINS ARRAY_SIZE(xiao_pins)

static int parse_pin(const char *name)
{
    if ((name[0] == 'D' || name[0] == 'd') && name[1] != '\0') {
        char *end;
        long idx = strtol(&name[1], &end, 10);

        if (*end == '\0' && idx >= 0 && idx < (long)NUM_XIAO_PINS) {
            return (int)idx;
        }
    }
    return -1;
}

static int parse_flags(const char *str, gpio_flags_t *flags)
{
    *flags = 0;

    for (size_t i = 0; i < strlen(str); i++) {
        switch (str[i]) {
        case 'i':
            *flags |= GPIO_INPUT;
            break;
        case 'o':
            *flags |= GPIO_OUTPUT;
            break;
        case 'u':
            *flags |= GPIO_PULL_UP;
            break;
        case 'd':
            *flags |= GPIO_PULL_DOWN;
            break;
        case 'h':
            *flags |= GPIO_ACTIVE_HIGH;
            break;
        case 'l':
            *flags |= GPIO_ACTIVE_LOW;
            break;
        case '0':
            *flags |= GPIO_OUTPUT_INIT_LOW;
            break;
        case '1':
            *flags |= GPIO_OUTPUT_INIT_HIGH;
            break;
        default:
            return -EINVAL;
        }
    }
    return 0;
}

/* kite gpio conf <pin> <flags> */
static int cmd_gpio_conf(const struct shell *sh, size_t argc, char **argv)
{
    int idx = parse_pin(argv[1]);

    if (idx < 0) {
        shell_error(sh, "Unknown pin '%s' (use D0-D%d)", argv[1],
                    NUM_XIAO_PINS - 1);
        return -EINVAL;
    }

    const struct gpio_dt_spec *spec = &xiao_pins[idx];

    if (!gpio_is_ready_dt(spec)) {
        shell_error(sh, "GPIO device not ready");
        return -ENODEV;
    }

    gpio_flags_t flags;
    int ret = parse_flags(argv[2], &flags);

    if (ret < 0) {
        shell_error(sh,
                    "Invalid flags '%s'\n"
                    "Use: i=input o=output u=pull-up "
                    "d=pull-down h=active-high l=active-low "
                    "0=init-low 1=init-high",
                    argv[2]);
        return ret;
    }

    ret = gpio_pin_configure_dt(spec, flags);
    if (ret < 0) {
        shell_error(sh, "Configure failed: %d", ret);
        return ret;
    }

    shell_print(sh, "D%d configured", idx);
    return 0;
}

/* kite gpio set <pin> <0|1> */
static int cmd_gpio_set(const struct shell *sh, size_t argc, char **argv)
{
    int idx = parse_pin(argv[1]);

    if (idx < 0) {
        shell_error(sh, "Unknown pin '%s' (use D0-D%d)", argv[1],
                    NUM_XIAO_PINS - 1);
        return -EINVAL;
    }

    const struct gpio_dt_spec *spec = &xiao_pins[idx];

    if (gpio_pin_is_output_dt(spec) != 1) {
        shell_error(sh, "D%d is not configured as output", idx);
        return -EACCES;
    }

    int ret;
    uint32_t val = shell_strtoul(argv[2], 0, &ret);

    if (ret < 0) {
        shell_error(sh, "Invalid value");
        return ret;
    }

    ret = gpio_pin_set_dt(spec, val != 0);
    if (ret < 0) {
        shell_error(sh, "Set failed: %d", ret);
        return ret;
    }

    shell_print(sh, "D%d = %u", idx, val);
    return 0;
}

/* kite gpio get <pin> */
static int cmd_gpio_get(const struct shell *sh, size_t argc, char **argv)
{
    int idx = parse_pin(argv[1]);

    if (idx < 0) {
        shell_error(sh, "Unknown pin '%s' (use D0-D%d)", argv[1],
                    NUM_XIAO_PINS - 1);
        return -EINVAL;
    }

    const struct gpio_dt_spec *spec = &xiao_pins[idx];

    if (gpio_pin_is_input_dt(spec) != 1) {
        shell_error(sh, "D%d is not configured as input", idx);
        return -EACCES;
    }

    int val = gpio_pin_get_dt(spec);

    if (val < 0) {
        shell_error(sh, "Get failed: %d", val);
        return val;
    }

    shell_print(sh, "D%d = %d", idx, val);
    return 0;
}

/* kite gpio toggle <pin> */
static int cmd_gpio_toggle(const struct shell *sh, size_t argc, char **argv)
{
    int idx = parse_pin(argv[1]);

    if (idx < 0) {
        shell_error(sh, "Unknown pin '%s' (use D0-D%d)", argv[1],
                    NUM_XIAO_PINS - 1);
        return -EINVAL;
    }

    const struct gpio_dt_spec *spec = &xiao_pins[idx];

    if (gpio_pin_is_output_dt(spec) != 1) {
        shell_error(sh, "D%d is not configured as output", idx);
        return -EACCES;
    }

    int ret = gpio_pin_toggle_dt(spec);

    if (ret < 0) {
        shell_error(sh, "Toggle failed: %d", ret);
        return ret;
    }

    shell_print(sh, "D%d toggled", idx);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_gpio_cmds,
    SHELL_CMD_ARG(conf, NULL,
                  "Configure a GPIO pin\n"
                  "Usage: kite gpio conf <D0-D10> <flags>\n"
                  "Flags: i=input o=output u=pull-up d=pull-down\n"
                  "       h=active-high l=active-low 0=init-low "
                  "1=init-high\n"
                  "Example: kite gpio conf D0 oh1",
                  cmd_gpio_conf, 3, 0),
    SHELL_CMD_ARG(set, NULL,
                  "Set GPIO output level\n"
                  "Usage: kite gpio set <D0-D10> <0|1>",
                  cmd_gpio_set, 3, 0),
    SHELL_CMD_ARG(get, NULL,
                  "Read GPIO pin level\n"
                  "Usage: kite gpio get <D0-D10>",
                  cmd_gpio_get, 2, 0),
    SHELL_CMD_ARG(toggle, NULL,
                  "Toggle GPIO output\n"
                  "Usage: kite gpio toggle <D0-D10>",
                  cmd_gpio_toggle, 2, 0),
    SHELL_SUBCMD_SET_END);

KITE_CMD_ADD(gpio, &kite_gpio_cmds, "GPIO commands", NULL);
