/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NUS Central Sample
 *
 * Demonstrates using the BLE NUS (Nordic UART Service) from the central role.
 * This application scans for a peripheral advertising the NUS service UUID,
 * connects, discovers the NUS TX characteristic, subscribes to notifications,
 * and prints received data. A shell command is provided to send data back.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(nus_central, LOG_LEVEL_INF);

/* ==========================================================================
 * NUS UUIDs
 *
 * Zephyr provides these via <zephyr/bluetooth/services/nus.h>:
 *   BT_UUID_NUS_SRV_VAL   - NUS Service         (6e400001-...)
 *   BT_UUID_NUS_TX_CHAR_VAL - NUS TX Characteristic (6e400003-...)
 *   BT_UUID_NUS_RX_CHAR_VAL - NUS RX Characteristic (6e400002-...)
 *
 * From the peripheral's perspective:
 *   TX char = peripheral sends notifications to us (central subscribes here)
 *   RX char = peripheral receives writes from us  (central writes here)
 * ========================================================================== */

/* ==========================================================================
 * State
 * ========================================================================== */

static struct bt_conn *default_conn;

static struct bt_uuid_128 discover_uuid;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static uint16_t nus_rx_handle; /* Handle for writing to peripheral's RX */

/* ==========================================================================
 * GATT Notification Callback
 * ========================================================================== */

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data) {
		LOG_WRN("Unsubscribed");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	LOG_INF("Received %u bytes:", length);
	LOG_HEXDUMP_INF(data, length, "NUS RX");

	/* Also print as string if it looks like text */
	const uint8_t *bytes = data;
	bool printable = true;

	for (uint16_t i = 0; i < length; i++) {
		if (bytes[i] < 0x20 && bytes[i] != '\n' && bytes[i] != '\r' &&
		    bytes[i] != '\t') {
			printable = false;
			break;
		}
	}

	if (printable && length > 0) {
		LOG_INF("  \"%.*s\"", length, (const char *)data);
	}

	return BT_GATT_ITER_CONTINUE;
}

/* ==========================================================================
 * GATT Discovery Callback
 *
 * Discovery proceeds in three stages:
 *   1. Discover NUS primary service
 *   2. Discover the TX characteristic (the one we subscribe to)
 *   3. Discover the CCC descriptor and subscribe to notifications
 * ========================================================================== */

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		LOG_WRN("Discovery complete (no more attributes)");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	LOG_INF("[ATTR] handle %u", attr->handle);

	/* Stage 1: Found NUS Service -> discover the TX characteristic */
	if (!bt_uuid_cmp(discover_params.uuid,
			 BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL))) {

		LOG_INF("NUS Service found");

		memcpy(&discover_uuid,
		       BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL),
		       sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("TX char discover failed (err %d)", err);
		}

	/* Stage 2: Found TX characteristic -> discover its CCC descriptor */
	} else if (!bt_uuid_cmp(discover_params.uuid,
				BT_UUID_DECLARE_128(BT_UUID_NUS_TX_CHAR_VAL))) {

		LOG_INF("NUS TX Characteristic found");

		subscribe_params.value_handle =
			bt_gatt_attr_value_handle(attr);

		/* Now find the CCC descriptor */
		memcpy(&discover_uuid, BT_UUID_GATT_CCC,
		       sizeof(struct bt_uuid_16));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("CCC discover failed (err %d)", err);
		}

	/* Stage 3: Found CCC descriptor -> subscribe to notifications */
	} else {
		LOG_INF("CCC found, subscribing to notifications");

		subscribe_params.notify = notify_func;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			LOG_ERR("Subscribe failed (err %d)", err);
		} else {
			LOG_INF("Subscribed to NUS TX notifications");
		}
	}

	return BT_GATT_ITER_STOP;
}

/* ==========================================================================
 * GATT Write (for sending data to peripheral's RX characteristic)
 * ========================================================================== */

static void write_func(struct bt_conn *conn, uint8_t err,
		       struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Write failed (err %u)", err);
	} else {
		LOG_INF("Write complete (%u bytes)", params->length);
	}
}

static struct bt_gatt_write_params write_params;

static int nus_send(const uint8_t *data, uint16_t len)
{
	if (!default_conn) {
		LOG_ERR("Not connected");
		return -ENOTCONN;
	}

	if (nus_rx_handle == 0) {
		LOG_ERR("NUS RX handle not discovered");
		return -EINVAL;
	}

	write_params.func = write_func;
	write_params.handle = nus_rx_handle;
	write_params.offset = 0;
	write_params.data = data;
	write_params.length = len;

	return bt_gatt_write(default_conn, &write_params);
}

/* ==========================================================================
 * NUS RX Handle Discovery
 *
 * After subscribing to TX notifications, we also discover the RX
 * characteristic so we can write data to the peripheral.
 * ========================================================================== */

static struct bt_uuid_128 rx_discover_uuid;
static struct bt_gatt_discover_params rx_discover_params;

static uint8_t rx_discover_func(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	if (!attr) {
		LOG_WRN("NUS RX characteristic not found");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	nus_rx_handle = bt_gatt_attr_value_handle(attr);
	LOG_INF("NUS RX Characteristic found (handle %u) - ready to send",
		nus_rx_handle);

	return BT_GATT_ITER_STOP;
}

static void discover_nus_rx(struct bt_conn *conn)
{
	int err;

	memcpy(&rx_discover_uuid,
	       BT_UUID_DECLARE_128(BT_UUID_NUS_RX_CHAR_VAL),
	       sizeof(rx_discover_uuid));

	rx_discover_params.uuid = &rx_discover_uuid.uuid;
	rx_discover_params.func = rx_discover_func;
	rx_discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	rx_discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	rx_discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(conn, &rx_discover_params);
	if (err) {
		LOG_ERR("RX discover failed (err %d)", err);
	}
}

/* ==========================================================================
 * Scanning
 * ========================================================================== */

static void start_scan(void);

static bool parse_ad_for_nus(struct bt_data *data, void *user_data)
{
	bool *found = user_data;

	if (data->type == BT_DATA_UUID128_ALL ||
	    data->type == BT_DATA_UUID128_SOME) {
		/* Each entry is 16 bytes (128-bit UUID) */
		if (data->data_len % 16 != 0) {
			return true;
		}

		for (uint16_t i = 0; i < data->data_len; i += 16) {
			struct bt_uuid_128 uuid;

			if (!bt_uuid_create(&uuid.uuid, &data->data[i], 16)) {
				continue;
			}

			if (!bt_uuid_cmp(&uuid.uuid,
					 BT_UUID_DECLARE_128(
						BT_UUID_NUS_SRV_VAL))) {
				*found = true;
				return false; /* Stop parsing */
			}
		}
	}

	return true; /* Continue parsing */
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (default_conn) {
		return;
	}

	/* Accept connectable adverts and their scan responses */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
	    type != BT_GAP_ADV_TYPE_SCAN_RSP) {
		return;
	}

	/* Check if this packet contains the NUS service UUID */
	bool has_nus = false;

	bt_data_parse(ad, parse_ad_for_nus, &has_nus);
	if (!has_nus) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("NUS peripheral found: %s (RSSI %d)", addr_str, rssi);

	if (bt_le_scan_stop()) {
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
	if (err) {
		LOG_ERR("Create conn to %s failed (%d)", addr_str, err);
		start_scan();
	}
}

static void start_scan(void)
{
	int err;

	struct bt_le_scan_param scan_param = {
		.type     = BT_LE_SCAN_TYPE_ACTIVE,
		.options  = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window   = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for NUS peripherals...");
}

/* ==========================================================================
 * Data Length Extension and MTU Exchange
 *
 * These are two separate negotiations:
 *   1. DLE  -- over-the-air packet size (bt_conn_le_data_len_update)
 *   2. MTU  -- GATT payload size        (bt_gatt_exchange_mtu)
 * Both must be requested; each result is min(ours, theirs).
 * ========================================================================== */

static void update_data_length(struct bt_conn *conn)
{
	int err;
	struct bt_conn_le_data_len_param dl_param = {
		.tx_max_len = BT_GAP_DATA_LEN_MAX,
		.tx_max_time = BT_GAP_DATA_TIME_MAX,
	};

	err = bt_conn_le_data_len_update(conn, &dl_param);
	if (err) {
		LOG_ERR("Data length update failed (err %d)", err);
	}
}

static void on_le_data_len_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	LOG_INF("Data length updated: TX %u bytes (%u us), RX %u bytes (%u us)",
		info->tx_max_len, info->tx_max_time,
		info->rx_max_len, info->rx_max_time);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	if (err) {
		LOG_ERR("MTU exchange failed (err %u)", err);
	} else {
		uint16_t payload_mtu = bt_gatt_get_mtu(conn) - 3;

		LOG_INF("MTU exchange successful: ATT MTU %u, payload %u bytes",
			bt_gatt_get_mtu(conn), payload_mtu);
	}
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
	.func = mtu_exchange_cb,
};

/* ==========================================================================
 * Connection Callbacks
 * ========================================================================== */

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_ERR("Failed to connect to %s (err %u)", addr, conn_err);
		bt_conn_unref(default_conn);
		default_conn = NULL;
		start_scan();
		return;
	}

	LOG_INF("Connected: %s", addr);

	/* Request data length extension (over-the-air packet size) */
	update_data_length(conn);

	/* Request MTU exchange (GATT payload size) */
	err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (err) {
		LOG_WRN("MTU exchange request failed (err %d)", err);
	}

	/* Start discovering NUS service */
	memcpy(&discover_uuid,
	       BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL),
	       sizeof(discover_uuid));
	discover_params.uuid = &discover_uuid.uuid;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		LOG_ERR("Service discover failed (err %d)", err);
		return;
	}

	/* Also discover the RX characteristic for sending data */
	discover_nus_rx(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	nus_rx_handle = 0;

	LOG_INF("Restarting scan...");
	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_data_len_updated = on_le_data_len_updated,
};

/* ==========================================================================
 * Shell Commands
 * ========================================================================== */

static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: nus send <string>");
		return -EINVAL;
	}

	const char *msg = argv[1];
	int err = nus_send(msg, strlen(msg));

	if (err) {
		shell_error(sh, "Send failed (err %d)", err);
	} else {
		shell_print(sh, "Sent %zu bytes", strlen(msg));
	}

	return err;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (default_conn) {
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(default_conn),
				  addr, sizeof(addr));
		shell_print(sh, "Connected to %s", addr);
		shell_print(sh, "  NUS TX subscribed: %s",
			    subscribe_params.value_handle ? "yes" : "no");
		shell_print(sh, "  NUS RX handle:     %s",
			    nus_rx_handle ? "discovered" : "not found");
		shell_print(sh, "  MTU:               %u",
			    bt_gatt_get_mtu(default_conn));
	} else {
		shell_print(sh, "Not connected (scanning)");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(nus_cmds,
	SHELL_CMD_ARG(send, NULL,
		      "Send a string to the peripheral\n"
		      "Usage: nus send <string>",
		      cmd_send, 2, 0),
	SHELL_CMD(status, NULL,
		  "Show NUS connection status",
		  cmd_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(nus, &nus_cmds, "NUS Central commands", NULL);

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth initialised");

	start_scan();

	return 0;
}
