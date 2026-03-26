/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(traffic_light, LOG_LEVEL_INF);

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_g =
    GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

/* State durations (ms) */
#define RED_DURATION_MS    3000
#define GREEN_DURATION_MS  3000
#define YELLOW_DURATION_MS 1000
#define FLASH_PERIOD_MS    500
#define TICK_MS            100

/* ========================================================================== */
/* State Machine Context                                                      */
/* ========================================================================== */

enum traffic_state {
    STATE_RED,
    STATE_GREEN,
    STATE_YELLOW,
    STATE_FLASHING,
    STATE_COUNT,
};

enum traffic_mode {
    MODE_NORMAL,
    MODE_FLASH,
};

static const char *const state_names[] = {
    [STATE_RED] = "RED",
    [STATE_GREEN] = "GREEN",
    [STATE_YELLOW] = "YELLOW",
    [STATE_FLASHING] = "FLASHING",
};

struct traffic_ctx {
    struct smf_ctx ctx;
    int64_t entered_at;
    enum traffic_mode mode;
    enum traffic_mode requested_mode;
    bool flash_on;
};

static struct traffic_ctx tl_ctx;

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

static void red_entry(void *obj);
static enum smf_state_result red_run(void *obj);

static void green_entry(void *obj);
static enum smf_state_result green_run(void *obj);

static void yellow_entry(void *obj);
static enum smf_state_result yellow_run(void *obj);

static void flashing_entry(void *obj);
static enum smf_state_result flashing_run(void *obj);
static void flashing_exit(void *obj);

/* ========================================================================== */
/* State Table                                                                */
/* ========================================================================== */

static const struct smf_state states[] = {
    [STATE_RED] = SMF_CREATE_STATE(red_entry, red_run, NULL, NULL, NULL),

    [STATE_GREEN] = SMF_CREATE_STATE(green_entry, green_run, NULL, NULL, NULL),

    [STATE_YELLOW] =
        SMF_CREATE_STATE(yellow_entry, yellow_run, NULL, NULL, NULL),

    [STATE_FLASHING] = SMF_CREATE_STATE(flashing_entry, flashing_run,
                                        flashing_exit, NULL, NULL),
};

/* ========================================================================== */
/* LED Helpers                                                                */
/* ========================================================================== */

static void leds_set(int r, int g, int b)
{
    gpio_pin_set_dt(&led_r, r);
    gpio_pin_set_dt(&led_g, g);
    gpio_pin_set_dt(&led_b, b);
}

static void leds_off(void)
{
    leds_set(0, 0, 0);
}

/* ========================================================================== */
/* State Handlers                                                             */
/* ========================================================================== */

static void red_entry(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->entered_at = k_uptime_get();
    leds_set(1, 0, 0);
    LOG_INF("RED");
}

static enum smf_state_result red_run(void *obj)
{
    struct traffic_ctx *ctx = obj;

    if (ctx->requested_mode == MODE_FLASH) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_FLASHING]);
        return SMF_EVENT_HANDLED;
    }

    if ((k_uptime_get() - ctx->entered_at) >= RED_DURATION_MS) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_GREEN]);
    }

    return SMF_EVENT_HANDLED;
}

static void green_entry(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->entered_at = k_uptime_get();
    leds_set(0, 1, 0);
    LOG_INF("GREEN");
}

static enum smf_state_result green_run(void *obj)
{
    struct traffic_ctx *ctx = obj;

    if (ctx->requested_mode == MODE_FLASH) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_FLASHING]);
        return SMF_EVENT_HANDLED;
    }

    if ((k_uptime_get() - ctx->entered_at) >= GREEN_DURATION_MS) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_YELLOW]);
    }

    return SMF_EVENT_HANDLED;
}

static void yellow_entry(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->entered_at = k_uptime_get();
    leds_set(1, 1, 0);
    LOG_INF("YELLOW");
}

static enum smf_state_result yellow_run(void *obj)
{
    struct traffic_ctx *ctx = obj;

    if (ctx->requested_mode == MODE_FLASH) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_FLASHING]);
        return SMF_EVENT_HANDLED;
    }

    if ((k_uptime_get() - ctx->entered_at) >= YELLOW_DURATION_MS) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_RED]);
    }

    return SMF_EVENT_HANDLED;
}

static void flashing_entry(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->entered_at = k_uptime_get();
    ctx->mode = MODE_FLASH;
    ctx->flash_on = true;
    leds_set(1, 1, 0);
    LOG_INF("FLASHING");
}

static enum smf_state_result flashing_run(void *obj)
{
    struct traffic_ctx *ctx = obj;

    if (ctx->requested_mode == MODE_NORMAL) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_RED]);
        return SMF_EVENT_HANDLED;
    }

    if ((k_uptime_get() - ctx->entered_at) >= FLASH_PERIOD_MS) {
        ctx->entered_at = k_uptime_get();
        ctx->flash_on = !ctx->flash_on;
        if (ctx->flash_on) {
            leds_set(1, 1, 0);
        } else {
            leds_off();
        }
    }

    return SMF_EVENT_HANDLED;
}

static void flashing_exit(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->mode = MODE_NORMAL;
}

/* ========================================================================== */
/* Shell Commands                                                             */
/* ========================================================================== */

static enum traffic_state get_current_state(void)
{
    const struct smf_state *current = tl_ctx.ctx.current;

    for (int i = 0; i < STATE_COUNT; i++) {
        if (current == &states[i]) {
            return (enum traffic_state)i;
        }
    }

    return STATE_RED;
}

static int cmd_mode(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    if (strcmp(argv[1], "normal") == 0) {
        tl_ctx.requested_mode = MODE_NORMAL;
        shell_print(sh, "Mode set to normal");
    } else if (strcmp(argv[1], "flash") == 0) {
        tl_ctx.requested_mode = MODE_FLASH;
        shell_print(sh, "Mode set to flash");
    } else {
        shell_error(sh, "Unknown mode '%s' (use 'normal' or 'flash')", argv[1]);
        return -EINVAL;
    }

    return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    enum traffic_state current = get_current_state();

    shell_print(sh, "State: %s  Mode: %s", state_names[current],
                tl_ctx.mode == MODE_NORMAL ? "normal" : "flash");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(smf_cmds,
                               SHELL_CMD_ARG(mode, NULL,
                                             "Set traffic light mode\n"
                                             "Usage: smf mode <normal|flash>",
                                             cmd_mode, 2, 0),
                               SHELL_CMD(status, NULL,
                                         "Show current state and mode",
                                         cmd_status),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(smf, &smf_cmds, "Traffic light state machine", NULL);

/* ========================================================================== */
/* Initialisation                                                             */
/* ========================================================================== */

static int leds_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) ||
        !gpio_is_ready_dt(&led_b)) {
        LOG_ERR("LED GPIO devices not ready");
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

int main(void)
{
    int ret;

    ret = leds_init();
    if (ret < 0) {
        LOG_ERR("LED init failed: %d", ret);
        return ret;
    }

    tl_ctx.mode = MODE_NORMAL;
    tl_ctx.requested_mode = MODE_NORMAL;

    smf_set_initial(SMF_CTX(&tl_ctx), &states[STATE_RED]);

    while (1) {
        ret = smf_run_state(SMF_CTX(&tl_ctx));
        if (ret != 0) {
            LOG_ERR("State machine terminated: %d", ret);
            leds_off();
            break;
        }
        k_msleep(TICK_MS);
    }

    return 0;
}
