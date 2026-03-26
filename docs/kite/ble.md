# BLE

The `kite ble` command provides non-connectable BLE beacon advertising. You can
set a device name, load a custom manufacturer data payload, start advertising,
and update the payload live -- all from the shell. This is a minimal
introduction to Zephyr's Bluetooth API without the complexity of connections,
GATT services, or pairing.

**Source:** `lib/kite/ble/kite_ble.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite ble start [name]` | Start advertising (optional name, max 8 chars, default "kite") |
| `kite ble stop` | Stop advertising |
| `kite ble data <bytes...>` | Set manufacturer data payload (hex bytes, max 22) |

Examples:

```
uart:~$ kite ble start myboard
Advertising as 'myboard'

uart:~$ kite ble data 01 02 03 FF
Updated 4 mfr byte(s), advertising

uart:~$ kite ble stop
Advertising stopped
```

You can verify the advertisements with any BLE scanner app (e.g. nRF Connect)
on your phone. Look for the device name and the manufacturer data field.

## Kconfig

```cfg
CONFIG_BT=y              # enable Bluetooth stack
CONFIG_BT_BROADCASTER=y  # enable advertising role
CONFIG_CSSE4011_SHELL_BLE=y
```

Unlike the other subsystems that use `select` to pull in drivers, the BLE
Kconfig uses `depends on BT` and `depends on BT_BROADCASTER`. This is because
the Bluetooth stack is large and has its own configuration requirements --
force-selecting it could cause unexpected build issues, so the application must
opt in explicitly.

## Code Walkthrough

### Advertising Data Structure

BLE advertising packets are built from `bt_data` structs, each representing
one AD (Advertising Data) element:

```c
static char dev_name[MAX_NAME_LEN + 1] = "kite";
static uint8_t mfr_buf[2 + MAX_USER_MFR_LEN] = {MFR_COMPANY_ID_LO,
                                                 MFR_COMPANY_ID_HI}; // (1)!
static uint8_t mfr_len = 2;

static void build_ad(struct bt_data ad[2])
{
    ad[0].type = BT_DATA_NAME_COMPLETE; // (2)!
    ad[0].data = dev_name;
    ad[0].data_len = strlen(dev_name);

    ad[1].type = BT_DATA_MANUFACTURER_DATA; // (3)!
    ad[1].data = mfr_buf;
    ad[1].data_len = mfr_len;
}
```

1. The manufacturer data field starts with a 2-byte company ID. `0xFFFF` is
   the BLE SIG "test/unassigned" company ID -- safe to use for development.
   The user's payload bytes are appended after these two bytes.
2. `BT_DATA_NAME_COMPLETE` -- the device's human-readable name, visible in
   scanner apps.
3. `BT_DATA_MANUFACTURER_DATA` -- a free-form payload. This is how beacons
   typically transmit sensor data, status, or identifiers without
   establishing a connection.

### Lazy Bluetooth Init

The Bluetooth stack is initialised on first use:

```c
static bool bt_ready;

static int ensure_bt_ready(const struct shell *sh)
{
    if (bt_ready) {
        return 0;
    }

    int ret = bt_enable(NULL); // (1)!

    if (ret < 0) {
        shell_error(sh, "BT init failed: %d", ret);
        return ret;
    }

    bt_ready = true;
    return 0;
}
```

1. `bt_enable` initialises the Bluetooth controller and host stack. This
   takes a few hundred milliseconds and can only be called once. Passing
   `NULL` makes it synchronous (blocks until ready). You could pass a
   callback for asynchronous init, but for a shell command blocking is fine.

### Starting Advertising

```c
struct bt_data ad[2];

build_ad(ad);

ret = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0); // (1)!
```

1. `BT_LE_ADV_NCONN` configures non-connectable, non-scannable advertising.
   The device broadcasts its advertisement packets but rejects connection
   requests. The last two arguments (`NULL, 0`) are for scan response data,
   which we don't use.

### Live Payload Updates

The `data` command can update the manufacturer payload while advertising is
active:

```c
uint8_t *payload = &mfr_buf[2];

for (uint32_t i = 0; i < num_bytes; i++) {
    uint32_t val = shell_strtoul(argv[i + 1], 16, &ret);
    ...
    payload[i] = (uint8_t)val; // (1)!
}
mfr_len = 2 + num_bytes;

if (advertising) {
    struct bt_data ad[2];

    build_ad(ad);

    ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0); // (2)!
    ...
}
```

1. User bytes are written after the 2-byte company ID prefix in `mfr_buf`.
2. `bt_le_adv_update_data` swaps in the new advertising data without
   stopping and restarting the advertiser. The next advertisement packet
   will contain the updated payload.

If advertising hasn't started yet, the data is stored and will be included
when `start` is called.

## Try It

1. Start advertising:
   ```
   kite ble start sensor1
   ```
2. Open nRF Connect (or similar) on your phone and scan. You should see
   "sensor1" in the device list.
3. Set some payload data:
   ```
   kite ble data AA BB CC
   ```
4. In your scanner app, look at the manufacturer data field -- you should see
   `FF FF AA BB CC` (company ID + your bytes).
