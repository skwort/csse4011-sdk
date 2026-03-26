/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <string.h>

#include "../kite_shell.h"

#define MAX_NAME_LEN      8
#define MAX_USER_MFR_LEN  22
#define MFR_COMPANY_ID_LO 0xFF /* BLE SIG test/unassigned */
#define MFR_COMPANY_ID_HI 0xFF

static bool bt_ready;
static bool advertising;

static char dev_name[MAX_NAME_LEN + 1] = "kite";
static uint8_t mfr_buf[2 + MAX_USER_MFR_LEN] = {MFR_COMPANY_ID_LO,
                                                MFR_COMPANY_ID_HI};
static uint8_t mfr_len = 2; /* always includes company ID */

static void build_ad(struct bt_data ad[2])
{
    ad[0].type = BT_DATA_NAME_COMPLETE;
    ad[0].data = dev_name;
    ad[0].data_len = strlen(dev_name);

    ad[1].type = BT_DATA_MANUFACTURER_DATA;
    ad[1].data = mfr_buf;
    ad[1].data_len = mfr_len;
}

static int ensure_bt_ready(const struct shell *sh)
{
    if (bt_ready) {
        return 0;
    }

    int ret = bt_enable(NULL);

    if (ret < 0) {
        shell_error(sh, "BT init failed: %d", ret);
        return ret;
    }

    bt_ready = true;
    return 0;
}

/* kite ble start [name] */
static int cmd_ble_start(const struct shell *sh, size_t argc, char **argv)
{
    int ret = ensure_bt_ready(sh);

    if (ret < 0) {
        return ret;
    }

    if (advertising) {
        shell_error(sh, "Already advertising, stop first");
        return -EALREADY;
    }

    if (argc > 1) {
        if (strlen(argv[1]) > MAX_NAME_LEN) {
            shell_error(sh, "Name too long (max %d chars)", MAX_NAME_LEN);
            return -EINVAL;
        }
        strncpy(dev_name, argv[1], sizeof(dev_name));
    }

    struct bt_data ad[2];

    build_ad(ad);

    ret = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (ret < 0) {
        shell_error(sh, "Adv start failed: %d", ret);
        return ret;
    }

    advertising = true;
    shell_print(sh, "Advertising as '%s'", dev_name);
    return 0;
}

/* kite ble stop */
static int cmd_ble_stop(const struct shell *sh, size_t argc, char **argv)
{
    if (!advertising) {
        shell_error(sh, "Not advertising");
        return -EALREADY;
    }

    int ret = bt_le_adv_stop();

    if (ret < 0) {
        shell_error(sh, "Adv stop failed: %d", ret);
        return ret;
    }

    advertising = false;
    shell_print(sh, "Advertising stopped");
    return 0;
}

/* kite ble data <bytes...> */
static int cmd_ble_data(const struct shell *sh, size_t argc, char **argv)
{
    int ret = ensure_bt_ready(sh);

    if (ret < 0) {
        return ret;
    }

    uint32_t num_bytes = argc - 1;

    if (num_bytes > MAX_USER_MFR_LEN) {
        shell_error(sh, "Too many bytes (max %d)", MAX_USER_MFR_LEN);
        return -EINVAL;
    }

    uint8_t *payload = &mfr_buf[2];

    for (uint32_t i = 0; i < num_bytes; i++) {
        uint32_t val = shell_strtoul(argv[i + 1], 16, &ret);

        if (ret < 0 || val > 0xFF) {
            shell_error(sh, "Invalid byte '%s' (use hex 00-FF)", argv[i + 1]);
            return -EINVAL;
        }
        payload[i] = (uint8_t)val;
    }
    mfr_len = 2 + num_bytes;

    if (advertising) {
        struct bt_data ad[2];

        build_ad(ad);

        ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
        if (ret < 0) {
            shell_error(sh, "Adv update failed: %d", ret);
            return ret;
        }
        shell_print(sh, "Updated %u mfr byte(s), advertising", num_bytes);
    } else {
        shell_print(sh, "Set %u mfr byte(s), not yet advertising", num_bytes);
    }

    return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_ble_cmds,
    SHELL_CMD_ARG(start, NULL,
                  "Start non-connectable advertising\n"
                  "Usage: kite ble start [name]\n"
                  "  name - device name (max 8 chars, default 'kite')",
                  cmd_ble_start, 1, 1),
    SHELL_CMD_ARG(stop, NULL,
                  "Stop advertising\n"
                  "Usage: kite ble stop",
                  cmd_ble_stop, 1, 0),
    SHELL_CMD_ARG(data, NULL,
                  "Set manufacturer data payload\n"
                  "Usage: kite ble data <bytes...>\n"
                  "  bytes - hex values (00-FF), max 22\n"
                  "  Company ID (0xFFFF) is prepended automatically\n"
                  "  Updates live if already advertising",
                  cmd_ble_data, 2, 253),
    SHELL_SUBCMD_SET_END);
/* clang-format on */

KITE_CMD_ADD(ble, &kite_ble_cmds, "BLE commands", NULL);
