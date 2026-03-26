# PWM

The `kite pwm` command provides two modes of operation: raw PWM channel control
(set a period and duty cycle on any channel) and an RGB LED brightness mode
that drives the onboard LED via PWM instead of simple on/off GPIO.

**Source:** `lib/kite/pwm/kite_pwm.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite pwm set <channel> <period_us> <duty_us>` | Set PWM output on a channel |
| `kite pwm stop <channel>` | Stop PWM on a channel |
| `kite pwm led <r> <g> <b>` | Set RGB LED brightness (0-255 per channel) |

The `set`/`stop` commands are available when `kite-pwm` is defined in the
device tree. The `led` command is available when the `kite_pwm_red`,
`kite_pwm_green`, and `kite_pwm_blue` nodes exist.

Examples:

```
uart:~$ kite pwm set 0 1000 500
PWM ch0: period=1000us duty=500us

uart:~$ kite pwm stop 0
PWM ch0 stopped

uart:~$ kite pwm led 255 0 128
LED: R=255 G=0 B=128
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_PWM=y  # default y
```

Selects the `PWM` driver.

## Device Tree Setup

The overlay configures PWM for the RGB LED using the `pwm-leds` compatible
binding:

```dts
kite_pwm_leds {
    compatible = "pwm-leds";
    kite_pwm_red: pwm_red {
        pwms = <&pwm1 0 PWM_MSEC(20) PWM_POLARITY_INVERTED>;
    };
    kite_pwm_green: pwm_green {
        pwms = <&pwm1 1 PWM_MSEC(20) PWM_POLARITY_INVERTED>;
    };
    kite_pwm_blue: pwm_blue {
        pwms = <&pwm1 2 PWM_MSEC(20) PWM_POLARITY_INVERTED>;
    };
};
```

Three things to notice:

1. **`PWM_MSEC(20)`** -- the period is 20 ms (50 Hz).
2. **`PWM_POLARITY_INVERTED`** -- the XIAO BLE's RGB LED is common-anode,
   meaning the LED is *on* when the pin is driven *low*. Inverted polarity
   tells the PWM driver to flip the signal.
3. **Pin control** -- the `&pinctrl` section maps PWM1 channels 0-2 to the
   physical LED pins (P0.26, P0.30, P0.6) with `nordic,invert` set.

## Code Walkthrough

### Compile-time Feature Detection

The code uses device tree macros to determine which features are available at
compile time:

```c
#define HAS_PWM_DEV  DT_NODE_EXISTS(DT_ALIAS(kite_pwm))
#define HAS_PWM_LEDS DT_NODE_EXISTS(DT_NODELABEL(kite_pwm_red))
```

The raw channel commands (`set`/`stop`) are compiled under `#if HAS_PWM_DEV`;
the LED command under `#if HAS_PWM_LEDS`. The shell subcommand set uses the
same guards, so only the available commands show up in tab completion:

```c
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_pwm_cmds,
#if HAS_PWM_DEV
    SHELL_CMD_ARG(set, NULL, ..., cmd_pwm_set, 4, 0),
    SHELL_CMD_ARG(stop, NULL, ..., cmd_pwm_stop, 2, 0),
#endif
#if HAS_PWM_LEDS
    SHELL_CMD_ARG(led, NULL, ..., cmd_pwm_led, 4, 0),
#endif
    SHELL_SUBCMD_SET_END);
```

### LED Brightness Scaling

The `led` command maps a 0-255 user value to the PWM duty cycle:

```c
uint32_t duty_r = ((uint64_t)pwm_red.period * r) / 255;
```

`pwm_red.period` comes from the device tree spec (20 ms in nanoseconds). The
cast to `uint64_t` prevents overflow -- the period is 20,000,000 ns, and
`20000000 * 255` exceeds `UINT32_MAX`. When `r = 255`, the duty cycle equals
the full period (LED fully on). When `r = 0`, the duty is zero (LED fully off). Because the polarity is inverted in the
device tree, the PWM driver handles the inversion transparently --
`pwm_set_pulse_dt` sends the correct inverted waveform without the C code
needing to know about common-anode wiring.

### Raw Channel Control

The `set` command uses the lower-level `pwm_set()` API directly, which takes
the device pointer, channel number, and period/duty in microseconds:

```c
ret = pwm_set(dev, channel, PWM_USEC(period_us), PWM_USEC(duty_us), 0);
```

The `stop` command simply sets period and duty to zero:

```c
ret = pwm_set(dev, channel, 0, 0, 0);
```

!!! tip
    The raw `set` command is useful for driving external devices like servos
    or buzzers. A typical hobby servo expects a 20 ms period (20000 us) with a
    pulse width between 1000 us (0 degrees) and 2000 us (180 degrees):
    ```
    kite pwm set 0 20000 1500
    ```
