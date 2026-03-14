# IMU

The `kite imu` command reads the onboard LSM6DS3TR-C inertial measurement unit
on the XIAO BLE Sense. It provides 3-axis accelerometer and 3-axis gyroscope
readings through Zephyr's generic sensor API, so the code works the same way
regardless of which specific IMU chip is on the board.

**Source:** `lib/kite/imu/kite_imu.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite imu read` | Read both accelerometer and gyroscope |
| `kite imu accel` | Read accelerometer only (m/s^2) |
| `kite imu gyro` | Read gyroscope only (rad/s) |

Examples:

```
uart:~$ kite imu read
Accel (m/s^2):
  X: 0.314733
  Y: -0.194265
  Z: 9.698172
Gyro (rad/s):
  X: -0.012217
  Y: 0.003054
  Z: 0.001527

uart:~$ kite imu accel
  X: 0.311679
  Y: -0.197319
  Z: 9.701226
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_IMU=y  # default y
```

This selects both `SENSOR` (the generic sensor framework) and `LSM6DSL` (the
driver for the LSM6DS3TR-C, which uses the LSM6DSL driver family).

The sample `prj.conf` also configures the driver's output data rate:

```cfg
CONFIG_LSM6DSL_TRIGGER_NONE=y  # (1)!
CONFIG_LSM6DSL_ACCEL_ODR=4     # (2)!
CONFIG_LSM6DSL_GYRO_ODR=4
```

1. Disables the trigger/interrupt path -- we poll on demand from the shell,
   so there's no need for data-ready interrupts.
2. Sets the output data rate. The value `4` maps to 104 Hz in the LSM6DSL
   driver's ODR table. This is a reasonable default for interactive use.

## Hardware Notes

The LSM6DS3TR-C on the XIAO BLE Sense is connected to **I2C0** (the internal
bus), not the XIAO connector's I2C1 bus that `kite i2c` uses. The device is
defined in the board's base device tree (`xiao_ble_nrf52840_sense.dts`), so no
overlay configuration is needed -- the driver is ready as soon as it's enabled.

## Code Walkthrough

### Device Reference

The IMU device is obtained via its device tree node label:

```c
static const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));
```

This is different from the other subsystems that use `DT_ALIAS`. The IMU has a
fixed node label in the board's device tree, so there's no need for an alias
in the overlay.

### The Sensor API Pattern

Zephyr's sensor API uses a two-step fetch-then-get pattern. The `read` command
demonstrates both steps:

```c
int ret = sensor_sample_fetch(imu_dev); // (1)!
...
struct sensor_value accel[3];
struct sensor_value gyro[3];

ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel); // (2)!
...
ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
```

1. `sensor_sample_fetch` triggers a full sample from the hardware. The driver
   reads all channels from the IMU over I2C and stores the results internally.
   This is a blocking call -- it waits for the I2C transfer to complete.
2. `sensor_channel_get` retrieves the fetched data for a specific channel.
   `SENSOR_CHAN_ACCEL_XYZ` populates a 3-element array with X, Y, Z
   acceleration. No I2C traffic happens here -- it just reads from the
   driver's internal buffer.

The `accel` and `gyro` commands use `sensor_sample_fetch_chan` instead, which
fetches only the requested channel:

```c
ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ); // (1)!
```

1. Only fetches accelerometer data, skipping the gyroscope. This is
   slightly more efficient if you only need one sensor.

### sensor_value Format

Zephyr represents sensor readings as a `struct sensor_value` with two integer
fields:

```c
struct sensor_value {
    int32_t val1;  /* integer part */
    int32_t val2;  /* fractional part in millionths */
};
```

So a value of `9.698172 m/s^2` is stored as `val1 = 9`, `val2 = 698172`. The
helper function handles the sign of `val2` for negative values:

```c
static void print_sensor_val(const struct shell *sh, const char *label,
                             struct sensor_value *val)
{
    bool neg = (val->val1 < 0) || (val->val2 < 0);
    int32_t abs_val1 = val->val1 < 0 ? -val->val1 : val->val1;
    int32_t abs_val2 = val->val2 < 0 ? -val->val2 : val->val2;

    shell_print(sh, "  %s: %s%d.%06d", label, neg ? "-" : "",
                abs_val1, abs_val2); // (1)!
}
```

1. For negative values like `-0.194265`, `val1 = 0` and `val2 = -194265`.
   The sign is carried by whichever field is non-zero, so we check both
   and print a leading minus when either is negative.
