/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <stdlib.h>

#include "../kite_shell.h"

/*
 * XIAO BLE analog pin to nRF SAADC AIN channel mapping.
 * Channels are defined in the board overlay under &adc and
 * referenced via the zephyr,user io-channels property.
 *
 * Channel 0: D0 (P0.02) = AIN0
 * Channel 1: D1 (P0.03) = AIN1
 * Channel 2: D2 (P0.28) = AIN4
 * Channel 3: D3 (P0.29) = AIN5
 */

#define DT_SPEC_AND_COMMA(node_id, prop, idx)                                  \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* clang-format off */
static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
                         DT_SPEC_AND_COMMA)
};
/* clang-format on */

#define NUM_ADC_CHANNELS ARRAY_SIZE(adc_channels)

static const char *pin_names[] = {"D0", "D1", "D2", "D3"};

static bool channels_configured;

static int ensure_channels_configured(const struct shell *sh)
{
    if (channels_configured) {
        return 0;
    }

    for (size_t i = 0; i < NUM_ADC_CHANNELS; i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            shell_error(sh, "ADC device not ready");
            return -ENODEV;
        }

        int ret = adc_channel_setup_dt(&adc_channels[i]);

        if (ret < 0) {
            shell_error(sh, "Channel %zu setup failed: %d", i, ret);
            return ret;
        }
    }

    channels_configured = true;
    return 0;
}

static int read_channel(const struct shell *sh, size_t idx, int16_t *raw,
                        int32_t *mv)
{
    uint16_t buf;
    struct adc_sequence seq = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    adc_sequence_init_dt(&adc_channels[idx], &seq);

    int ret = adc_read_dt(&adc_channels[idx], &seq);

    if (ret < 0) {
        shell_error(sh, "ADC read failed: %d", ret);
        return ret;
    }

    *raw = (int16_t)buf;
    *mv = (int32_t)buf;
    adc_raw_to_millivolts_dt(&adc_channels[idx], mv);

    return 0;
}

static int parse_analog_pin(const char *name)
{
    if ((name[0] == 'D' || name[0] == 'd') && name[1] != '\0') {
        char *end;
        long idx = strtol(&name[1], &end, 10);

        if (*end == '\0' && idx >= 0 && idx < (long)NUM_ADC_CHANNELS) {
            return (int)idx;
        }
    }
    return -1;
}

/* kite adc read <pin> */
static int cmd_adc_read(const struct shell *sh, size_t argc, char **argv)
{
    int ret = ensure_channels_configured(sh);

    if (ret < 0) {
        return ret;
    }

    int idx = parse_analog_pin(argv[1]);

    if (idx < 0) {
        shell_error(sh, "Unknown pin '%s' (use D0-D3)", argv[1]);
        return -EINVAL;
    }

    int16_t raw;
    int32_t mv;

    ret = read_channel(sh, idx, &raw, &mv);
    if (ret < 0) {
        return ret;
    }

    shell_print(sh, "%s: %d raw, %d mV", pin_names[idx], raw, mv);
    return 0;
}

/* kite adc read_all */
static int cmd_adc_read_all(const struct shell *sh, size_t argc, char **argv)
{
    int ret = ensure_channels_configured(sh);

    if (ret < 0) {
        return ret;
    }

    for (size_t i = 0; i < NUM_ADC_CHANNELS; i++) {
        int16_t raw;
        int32_t mv;

        ret = read_channel(sh, i, &raw, &mv);
        if (ret < 0) {
            shell_error(sh, "%s: error %d", pin_names[i], ret);
            continue;
        }

        shell_print(sh, "%s: %4d raw, %4d mV", pin_names[i], raw, mv);
    }

    return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_adc_cmds,
    SHELL_CMD_ARG(read, NULL,
                  "Read analog value from pin\n"
                  "Usage: kite adc read <D0-D3>",
                  cmd_adc_read, 2, 0),
    SHELL_CMD_ARG(read_all, NULL,
                  "Read all analog pins (D0-D3)\n"
                  "Usage: kite adc read_all",
                  cmd_adc_read_all, 1, 0),
    SHELL_SUBCMD_SET_END);
/* clang-format on */

KITE_CMD_ADD(adc, &kite_adc_cmds, "ADC commands", NULL);
