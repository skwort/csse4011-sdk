# I2C

The `kite i2c` command provides raw I2C bus access -- scanning for devices,
reading bytes, and writing bytes. It operates on the bus defined by the
`kite-i2c` device tree alias, which maps to the XIAO connector's I2C pins
(D4 = SDA, D5 = SCL).

**Source:** `lib/kite/i2c/kite_i2c.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite i2c scan` | Scan the bus for devices (address range 0x03-0x77) |
| `kite i2c read <addr> <len>` | Read N bytes from a device (addr in hex) |
| `kite i2c write <addr> <data...>` | Write hex bytes to a device |

All addresses and data bytes are specified in hexadecimal.

Examples:

```
uart:~$ kite i2c scan
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- 44 -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
2 device(s) found

uart:~$ kite i2c write 44 FD
Wrote 1 byte(s) to 0x44

uart:~$ kite i2c read 44 6
00: 6a 7a 19 85 33 43
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_I2C=y  # default y
```

Selects the `I2C` driver.

## Code Walkthrough

### Device Pointer

Unlike the GPIO and PWM subsystems that use `_DT_SPEC_GET` macros, the I2C
subsystem gets a raw device pointer at file scope:

```c
static const struct device *i2c_dev = DEVICE_DT_GET(DT_ALIAS(kite_i2c));
```

`DEVICE_DT_GET` resolves the device at compile time via the linker. The device
is not "opened" or "initialised" by this call -- Zephyr's driver init system
handles that at boot. The `device_is_ready()` check in each command handler
verifies that the driver initialised successfully.

### Bus Scan

The scan probes every address from `0x03` to `0x77` (the standard 7-bit
address range, excluding reserved addresses) by sending a zero-length write:

```c
struct i2c_msg msg = {
    .buf = NULL,
    .len = 0,
    .flags = I2C_MSG_WRITE | I2C_MSG_STOP, // (1)!
};
int ret = i2c_transfer(i2c_dev, &msg, 1, addr);
```

1. A zero-length write with a STOP condition is the standard I2C "ping" --
   the controller sends the address byte and checks for an ACK. If the
   device ACKs, `i2c_transfer` returns 0.

The output format matches the widely-used `i2cdetect` layout, making it
familiar if you've used Linux I2C tools.

### Read

```c
uint8_t buf[256];
ret = i2c_read(i2c_dev, buf, len, (uint16_t)addr);
```

`i2c_read` is a convenience wrapper around `i2c_transfer` with `I2C_MSG_READ |
I2C_MSG_STOP`. The data is printed in a hex dump format, 16 bytes per line.

Note that this is a *raw* read with no register address. Many I2C devices
expect a register address to be written first (a "combined write-then-read"
transaction). For those devices, use `kite i2c write` to send the register
address, then `kite i2c read` to get the data -- or extend the code with a
`write_read` command using `i2c_write_read()`.

### Write

The write command parses a variable number of hex byte arguments from the
shell:

```c
uint32_t num_bytes = argc - 2; // (1)!

uint8_t buf[256];

for (uint32_t i = 0; i < num_bytes; i++) {
    uint32_t val = shell_strtoul(argv[2 + i], 16, &ret);
    ...
    buf[i] = (uint8_t)val;
}

ret = i2c_write(i2c_dev, buf, num_bytes, (uint16_t)addr);
```

1. `argc` includes the command name and address, so `argc - 2` gives the
   number of data bytes. The command is registered with `3` mandatory args
   (command + addr + at least one byte) and `254` optional args, allowing
   up to 256 bytes total.

!!! note
    The onboard IMU (LSM6DS3TR-C at 0x6A) on the Sense variant is on a
    separate I2C bus (I2C0), not the XIAO connector bus (I2C1) that
    `kite-i2c` uses. A `kite i2c scan` will only show devices you've
    connected externally to D4 (SDA) and D5 (SCL).
