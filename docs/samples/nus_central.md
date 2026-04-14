# NUS Central

This sample demonstrates how to use BLE as a **central** device to connect
to a peripheral running the
[Nordic UART Service (NUS)](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/shell/nus.html).
It covers scanning, connecting, GATT service discovery, subscribing to
notifications, and sending data -- the key building blocks for any
central-side BLE application.

**Source:** `samples/nus_central/src/main.c`

## How It Works

```
                Central (this sample)              Peripheral (NUS)
               ========================          ====================

               1. Scan for NUS UUID
                  in advert / scan rsp
                        |
                        |  found
                        v
               2. Connect
                        |
                        v
               3. Discover NUS Service  -------->  NUS Service
                        |                          (6e400001-...)
                        v
               4. Discover TX Char     -------->  TX Characteristic
                  + subscribe (CCC)                (6e400003-...)
                        |
                        v
               5. Discover RX Char     -------->  RX Characteristic
                  (for writing)                    (6e400002-...)
                        |
                        v
               6. Ready!
                  - Notifications arrive via TX
                  - Data sent via write to RX
```

**NUS UUID naming convention:** TX and RX are named from the
**peripheral's** perspective. The peripheral *transmits* on the TX
characteristic (notifications to us) and *receives* on the RX
characteristic (writes from us).

## Shell Commands

| Command | Description |
|---------|-------------|
| `nus status` | Show connection state, subscription status, and MTU |
| `nus send <string>` | Write a string to the peripheral's RX characteristic |

Examples:

```
uart:~$ nus status
Connected to D7:BA:ED:13:75:90 (random)
  NUS TX subscribed: yes
  NUS RX handle:     discovered
  MTU:               23

uart:~$ nus send hello
Sent 5 bytes
```

## Sample Output

Below is the log when connecting to Zephyr's `peripheral_nus` sample. Note
that DLE negotiates to 251 bytes, but the ATT MTU stays at the default 23
because the peripheral has no MTU configuration:

```
uart:~$ [00:00:09.982] <inf> nus_central: Connected: 38:44:BE:BA:02:12 (public)
uart:~$ [00:00:10.083] <inf> nus_central: Data length updated: TX 251 bytes (2120 us), RX 251 bytes (2120 us)
uart:~$ [00:00:10.084] <inf> nus_central: MTU exchange successful: ATT MTU 23, payload 20 bytes
uart:~$ [00:00:10.184] <inf> nus_central: [ATTR] handle 16
uart:~$ [00:00:10.184] <inf> nus_central: NUS Service found
uart:~$ [00:00:10.384] <inf> nus_central: [ATTR] handle 17
uart:~$ [00:00:10.384] <inf> nus_central: NUS TX Characteristic found
uart:~$ [00:00:10.583] <inf> nus_central: [ATTR] handle 19
uart:~$ [00:00:10.583] <inf> nus_central: CCC found, subscribing to notifications
uart:~$ [00:00:10.584] <inf> nus_central: Subscribed to NUS TX notifications
uart:~$ [00:00:10.984] <inf> nus_central: NUS RX Characteristic found (handle 21) - ready to send
uart:~$ [00:00:12.933] <inf> nus_central: Received 13 bytes:
uart:~$ [00:00:12.934] <inf> nus_central: NUS RX
                                    48 65 6c 6c 6f 20 57 6f  72 6c 64 21 0a |Hello Wo rld!.
uart:~$ [00:00:12.934] <inf> nus_central:   "Hello World!"
```

## Build and Flash

```bash
cd samples/nus_central
west build -b xiao_ble/nrf52840/sense
west flash
```

You will need a **second device** running a NUS peripheral. Zephyr ships
one at `samples/bluetooth/peripheral_nus` that sends "Hello World!" every
three seconds:

```bash
cd $ZEPHYR_BASE/samples/bluetooth/peripheral_nus
west build -b <your_peripheral_board>
west flash
```

## Kconfig

```cfg
CONFIG_BT=y              # Bluetooth stack
CONFIG_BT_CENTRAL=y      # central role support
CONFIG_BT_GATT_CLIENT=y  # GATT client (discover, subscribe, write)
CONFIG_BT_SMP=y          # security manager (required by some stacks)
CONFIG_BT_MAX_CONN=1     # one connection at a time
CONFIG_SHELL=y           # shell commands
CONFIG_LOG=y             # logging output
```

!!! note "MTU and Data Length"
    After connecting, the central requests an ATT MTU exchange to allow
    larger payloads. However, the negotiated MTU is the **minimum** of what
    both sides support, and both sides also need larger buffers configured.
    By default, Zephyr uses a 23-byte ATT MTU and 27-byte data length, so
    the exchange alone won't help unless **both** the central and peripheral
    add Kconfig options like:

    ```cfg
    CONFIG_BT_USER_DATA_LEN_UPDATE=y
    CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
    CONFIG_BT_BUF_ACL_RX_SIZE=251
    CONFIG_BT_BUF_ACL_TX_SIZE=251
    CONFIG_BT_L2CAP_TX_MTU=247
    ```

    This enables Data Length Extension (DLE), increases the over-the-air
    packet size to 251 bytes, sizes the ACL buffers to match, and sets
    the L2CAP MTU to 247 (251 minus 4-byte L2CAP header). The usable ATT
    payload is then 244 bytes (247 minus 3-byte ATT header).

    See the
    [Nordic Academy lesson on connection parameters](https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-3-bluetooth-le-connections/topic/connection-parameters/)
    for more detail on MTU negotiation and data length extension.

## Code Walkthrough

### Scanning for NUS Peripherals

The scan callback fires for every received advertisement and scan
response. We parse the packet looking for the 128-bit NUS service UUID:

```c
static bool parse_ad_for_nus(struct bt_data *data, void *user_data)
{
    bool *found = user_data;

    if (data->type == BT_DATA_UUID128_ALL ||
        data->type == BT_DATA_UUID128_SOME) {

        for (uint16_t i = 0; i < data->data_len; i += 16) {
            struct bt_uuid_128 uuid;

            if (!bt_uuid_create(&uuid.uuid, &data->data[i], 16)) {
                continue;
            }

            if (!bt_uuid_cmp(&uuid.uuid,
                     BT_UUID_DECLARE_128(BT_UUID_NUS_SRV_VAL))) {
                *found = true;
                return false;
            }
        }
    }

    return true;
}
```

!!! note
    The Zephyr `peripheral_nus` sample places the NUS UUID in the **scan
    response** data, not the main advertising payload. That is why
    `device_found` accepts `BT_GAP_ADV_TYPE_SCAN_RSP` in addition to
    `BT_GAP_ADV_TYPE_ADV_IND` -- without this, the central would never
    see the UUID and would never connect.

### GATT Discovery Chain

After connecting, we kick off a three-stage discovery chain. Each stage's
completion triggers the next:

```c
discover_params.uuid = &nus_uuid.uuid;       // (1)!
discover_params.func = discover_func;
discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
discover_params.type = BT_GATT_DISCOVER_PRIMARY;

bt_gatt_discover(conn, &discover_params);
```

1. We reuse the same `discover_params` struct across all three stages.
   The `discover_func` callback updates its fields and calls
   `bt_gatt_discover` again for the next stage.

The three stages inside `discover_func`:

| Stage | Discovers | Then |
|-------|-----------|------|
| 1 | NUS primary service | Searches for TX characteristic |
| 2 | TX characteristic | Records `value_handle`, searches for CCC descriptor |
| 3 | CCC descriptor | Subscribes to notifications |

### Subscribing to Notifications

Once the CCC descriptor is found, we subscribe:

```c
subscribe_params.notify = notify_func;   // (1)!
subscribe_params.value = BT_GATT_CCC_NOTIFY;
subscribe_params.ccc_handle = attr->handle;

bt_gatt_subscribe(conn, &subscribe_params);
```

1. `notify_func` is called every time the peripheral sends a notification
   on the TX characteristic. This is where received data is logged.

### Sending Data

To send data back to the peripheral, we write to the **RX**
characteristic (discovered separately via `discover_nus_rx`):

```c
write_params.handle = nus_rx_handle;
write_params.data = data;
write_params.length = len;

bt_gatt_write(default_conn, &write_params);
```

### Reconnection

On disconnect, the callback unrefs the connection, resets state, and
restarts scanning automatically:

```c
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    bt_conn_unref(default_conn);
    default_conn = NULL;
    nus_rx_handle = 0;

    start_scan();
}
```

## Try It

1. Flash a NUS peripheral on one board (e.g. Zephyr's `peripheral_nus`).
2. Flash this sample on a second board.
3. Watch the log -- you should see the connection and "Hello World!"
   notifications arriving every 3 seconds.
4. Run `nus status` to inspect the connection.
5. Run `nus send hi` to write data back to the peripheral.
6. Power-cycle the peripheral and confirm the central reconnects
   automatically.
