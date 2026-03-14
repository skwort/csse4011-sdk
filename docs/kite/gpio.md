# GPIO

The `kite gpio` command gives you direct control over the 11 general-purpose
I/O pins on the XIAO BLE connector (D0 through D10). You can configure a pin's
direction and pull resistors, then read, write, or toggle it -- all from the
shell.

**Source:** `lib/kite/gpio/kite_gpio.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite gpio conf <pin> <flags>` | Configure pin direction and electrical properties |
| `kite gpio set <pin> <0\|1>` | Drive an output pin high or low |
| `kite gpio get <pin>` | Read the logic level of an input pin |
| `kite gpio toggle <pin>` | Toggle an output pin |

Pin names use the `D` prefix: `D0`, `D1`, ..., `D10` (case-insensitive).

### Flag Characters

The `conf` command accepts a compact flag string where each character maps to a
Zephyr GPIO flag:

| Char | Flag | Meaning |
|------|------|---------|
| `i` | `GPIO_INPUT` | Configure as input |
| `o` | `GPIO_OUTPUT` | Configure as output |
| `u` | `GPIO_PULL_UP` | Enable internal pull-up resistor |
| `d` | `GPIO_PULL_DOWN` | Enable internal pull-down resistor |
| `h` | `GPIO_ACTIVE_HIGH` | Logic level matches electrical level |
| `l` | `GPIO_ACTIVE_LOW` | Logic level is inverted |
| `0` | `GPIO_OUTPUT_INIT_LOW` | Initial output state: low |
| `1` | `GPIO_OUTPUT_INIT_HIGH` | Initial output state: high |

Combine them as needed. For example, `oh1` means "output, active-high,
initially high".

Examples:

```
uart:~$ kite gpio conf D0 oh1
D0 configured

uart:~$ kite gpio set D0 0
D0 = 0

uart:~$ kite gpio conf D3 iud
D3 configured

uart:~$ kite gpio get D3
D3 = 1

uart:~$ kite gpio toggle D0
D0 toggled
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_GPIO=y  # default y
```

This selects `GPIO` and `GPIO_GET_DIRECTION` (needed for the `set`/`get`/`toggle`
commands to verify the pin is configured in the right direction before operating
on it).

## Code Walkthrough

### Pin Table from Device Tree

The overlay defines 11 GPIO nodes under `xiao_gpio_pins` with aliases
`xiao-d0` through `xiao-d10`. The C code builds a compile-time array from
these:

```c
#define XIAO_PIN_SPEC(n) [n] = GPIO_DT_SPEC_GET(DT_ALIAS(xiao_d##n), gpios),

static const struct gpio_dt_spec xiao_pins[] = {
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d0))
    XIAO_PIN_SPEC(0)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(xiao_d1))
    XIAO_PIN_SPEC(1)
#endif
    ...
};
```

Each `#if DT_NODE_EXISTS(...)` guard means you can define fewer pins in your
overlay without compile errors. The designated initialiser `[n] = ...` ensures
the array index matches the pin number regardless of which pins are present.

### The Flag Parser

The `parse_flags` function converts the user's flag string into a bitmask:

```c
static int parse_flags(const char *str, gpio_flags_t *flags)
{
    *flags = 0;

    for (size_t i = 0; i < strlen(str); i++) {
        switch (str[i]) {
        case 'i': *flags |= GPIO_INPUT;           break;
        case 'o': *flags |= GPIO_OUTPUT;          break;
        case 'u': *flags |= GPIO_PULL_UP;         break;
        case 'd': *flags |= GPIO_PULL_DOWN;       break;
        case 'h': *flags |= GPIO_ACTIVE_HIGH;     break;
        case 'l': *flags |= GPIO_ACTIVE_LOW;      break;
        case '0': *flags |= GPIO_OUTPUT_INIT_LOW;  break;
        case '1': *flags |= GPIO_OUTPUT_INIT_HIGH; break;
        default:  return -EINVAL;
        }
    }
    return 0;
}
```

The resulting `gpio_flags_t` bitmask is passed directly to
`gpio_pin_configure_dt(spec, flags)`. This replaces the device tree flags
entirely for that pin -- the spec still provides the port and pin number, but
the user controls the electrical configuration.

### Direction Checks

Before writing to a pin, the code verifies it's actually configured as an
output:

```c
if (gpio_pin_is_output_dt(spec) != 1) {
    shell_error(sh, "D%d is not configured as output", idx);
    return -EACCES;
}
```

Similarly, `get` checks for `gpio_pin_is_input_dt`. This prevents confusing
errors from the GPIO driver and gives the user a clear message about what went
wrong. This is why the Kconfig selects `GPIO_GET_DIRECTION` -- without it,
these direction-query functions are not available.

!!! note
    Pins D4 and D5 are shared with the I2C bus (SDA/SCL). If you're using the
    I2C subsystem, avoid reconfiguring these pins with `kite gpio conf`.
