# Kite Shell

The **Kite Shell** is a single, self-contained sample application that serves
as a reference for the Zephyr peripheral APIs used in CSSE4011. Each subsystem
(LED, GPIO, PWM, UART, I2C, ADC, IMU, BLE) is implemented as an isolated module with its
own shell commands, so you can study one peripheral at a time without wading
through unrelated code.

The goal is to give you a stripped-down, readable example of each driver API
paired with an interactive test harness -- build it, flash it, and poke at
hardware from the serial console. The subsystem pages below walk through the
code line by line, explaining *why* things are done, not just *what*.

!!! tip "Going deeper"
    Kite Shell is intentionally simplified for teaching. Once you're comfortable
    with a subsystem here, look at the corresponding Zephyr shell and sample
    code for the full picture:

    - [Zephyr Shell Subsystem](https://docs.zephyrproject.org/latest/services/shell/index.html)
    - [Zephyr Samples](https://docs.zephyrproject.org/latest/samples/index.html)
      (especially under `drivers/` and `subsys/shell/`)
    - The built-in Zephyr shell commands (e.g. `gpio`, `i2c`, `pwm`) in
      `zephyr/subsys/shell/modules/` -- these are production-quality
      implementations of similar functionality.

Under the hood, Kite Shell is built on top of Zephyr's shell subsystem. Every
command lives under a single root: `kite`. Each peripheral registers its own
subcommands (`kite led`, `kite gpio`, etc.) and can be independently enabled
or disabled via Kconfig.

## Building the Kite Shell Sample

The quickest way to get started is with the provided sample application:

```bash
west build -b xiao_ble/nrf52840/sense csse4011-sdk/samples/kite_shell
```

Flash via UF2:

```bash
cp build/zephyr/zephyr.uf2 /mnt/xiao
```

Connect a serial terminal (115200 baud) and you should see:

```
CSSE4011 Kite Shell
uart:~$
```

Type `kite` and hit tab to see the available subcommands.

## Configuration

The sample's `prj.conf` enables everything:

```cfg
CONFIG_SHELL=y
CONFIG_CSSE4011=y
CONFIG_CSSE4011_SHELL=y
CONFIG_CSSE4011_SHELL_LED=y
CONFIG_CSSE4011_SHELL_GPIO=y
CONFIG_CSSE4011_SHELL_PWM=y
CONFIG_CSSE4011_SHELL_UART=y
CONFIG_CSSE4011_SHELL_I2C=y
CONFIG_CSSE4011_SHELL_ADC=y
CONFIG_CSSE4011_SHELL_IMU=y
CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_CSSE4011_SHELL_BLE=y
```

Each `CONFIG_CSSE4011_SHELL_*` option defaults to `y` when `CSSE4011_SHELL` is
enabled, so you only need to explicitly list them if you want to disable
specific subsystems. Each option also pulls in the required Zephyr driver via
`select` (e.g. `CSSE4011_SHELL_GPIO` selects `GPIO`), so you don't need to
enable drivers manually.

## Build System: How Kite is a Zephyr Library

The Kite Shell is built as a **Zephyr library** -- not a standalone application.
Understanding how the SDK plugs into the Zephyr build system is important
because it's the same pattern you'll use to extend it.

### The Big Picture

Zephyr's build system is CMake-based with a twist: it uses its own set of
CMake functions (`zephyr_library_*`) to manage compilation units, and a
parallel configuration system called **Kconfig** to control what gets built.
For any module in the SDK, you'll always see these two things working together:

- **CMakeLists.txt** decides *which source files* get compiled.
- **Kconfig** decides *whether* a module is enabled and pulls in driver
  dependencies.

### Top-Level Wiring

The SDK's top-level `CMakeLists.txt` is minimal:

```cmake
if(CONFIG_CSSE4011)
  add_subdirectory(lib/kite)
endif()
```

And the top-level `Kconfig`:

```kconfig
menuconfig CSSE4011
    bool "CSSE4011 SDK"

if CSSE4011
rsource "lib/kite/Kconfig"
endif
```

This is the entry point. When a `prj.conf` sets `CONFIG_CSSE4011=y`, CMake
descends into `lib/kite/` and Kconfig sources the kite menu. Nothing is
compiled unless the application opts in.

### The Kite Library

In `lib/kite/CMakeLists.txt`, the first line creates a named Zephyr library:

```cmake
zephyr_library_named(kite)  # (1)!

zephyr_library_sources_ifdef(CONFIG_CSSE4011_SHELL kite_shell.c)  # (2)!

add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_LED led)  # (3)!
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_GPIO gpio)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_PWM pwm)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_UART uart)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_I2C i2c)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_ADC adc)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_IMU imu)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_BLE ble)
```

1. `zephyr_library_named(kite)` creates a static library called `kite` that
   Zephyr will link into the final firmware image. All subsequent
   `zephyr_library_sources()` calls in this directory and its subdirectories
   add source files to this same library.
2. The shell scaffold (`kite_shell.c`) is only compiled when `CSSE4011_SHELL`
   is enabled.
3. Each subsystem directory is only entered if its Kconfig option is enabled.
   `add_subdirectory_ifdef` is a Zephyr CMake helper -- it's equivalent to
   `if(CONFIG_...) add_subdirectory(...) endif()`.

The corresponding `lib/kite/Kconfig` mirrors this structure:

```kconfig
menuconfig CSSE4011_SHELL
    bool "CSSE4011 Kite Shell Commands"
    depends on SHELL

if CSSE4011_SHELL
rsource "led/Kconfig"
rsource "gpio/Kconfig"
rsource "pwm/Kconfig"
rsource "uart/Kconfig"
rsource "i2c/Kconfig"
rsource "adc/Kconfig"
rsource "imu/Kconfig"
rsource "ble/Kconfig"
endif
```

`depends on SHELL` means the entire kite menu only appears if Zephyr's shell
subsystem is enabled. Each `rsource` pulls in the subsystem's Kconfig fragment.

### Subsystem Modules

Every subsystem follows the same two-file pattern. Taking LED as an example:

**`lib/kite/led/CMakeLists.txt`:**

```cmake
zephyr_library_sources(kite_led.c)
```

That's it -- one line. Because `zephyr_library_named(kite)` was called in the
parent directory, this `zephyr_library_sources()` adds `kite_led.c` to the
existing `kite` library. No need to create a new library per subsystem.

**`lib/kite/led/Kconfig`:**

```kconfig
config CSSE4011_SHELL_LED
    bool "Kite LED commands"
    default y
    select GPIO
```

Two things to note:

- **`default y`** -- subsystems are enabled by default when the parent
  `CSSE4011_SHELL` is active. Students don't have to remember to enable each
  one individually.
- **`select GPIO`** -- this is a Kconfig "hard dependency". When
  `CSSE4011_SHELL_LED` is enabled, the GPIO driver is automatically pulled in.
  This is different from `depends on GPIO`, which would require the *user* to
  enable `GPIO` first. Using `select` is the right call here because the LED
  code cannot function without GPIO -- there's no scenario where you'd want
  LED commands without the GPIO driver.

### The UART Exception

The UART subsystem's CMakeLists is slightly more interesting because it has
two mutually exclusive source files:

```cmake
zephyr_library_sources(kite_uart.c)
zephyr_library_sources_ifdef(CONFIG_CSSE4011_SHELL_UART_IRQ kite_uart_irq.c)
zephyr_library_sources_ifdef(CONFIG_CSSE4011_SHELL_UART_ASYNC kite_uart_async.c)
```

`kite_uart.c` (the shared scaffold) is always compiled when UART is enabled.
But exactly one of `kite_uart_irq.c` or `kite_uart_async.c` is compiled,
controlled by a Kconfig `choice`:

```kconfig
choice CSSE4011_SHELL_UART_API
    prompt "UART shell API mode"
    default CSSE4011_SHELL_UART_IRQ

config CSSE4011_SHELL_UART_IRQ
    bool "Interrupt-driven API"
    select UART_INTERRUPT_DRIVEN

config CSSE4011_SHELL_UART_ASYNC
    bool "Async/DMA API"
    depends on SERIAL_SUPPORT_ASYNC
    select UART_ASYNC_API
endchoice
```

A Kconfig `choice` enforces mutual exclusion -- exactly one option is active.
This is cleaner than using `#ifdef` in a single source file because each mode
is a self-contained compilation unit with no conditional compilation inside it.

### Why This Pattern Matters

This modular structure means:

- **Adding a subsystem** only touches its own directory plus two lines in the
  parent `CMakeLists.txt` and `Kconfig`. No existing code is modified.
- **Disabling a subsystem** is a single `CONFIG_*=n` in `prj.conf`. The code
  isn't compiled at all -- not just `#ifdef`'d out.
- **Driver dependencies** are handled by Kconfig `select`, so enabling a
  subsystem automatically pulls in everything it needs.
- **The final binary** only contains the subsystems you've enabled, keeping
  flash usage minimal.

## How Command Registration Works

Understanding the registration pattern is useful if you want to add your own
commands. The system has three layers:

**1. The root command** is created in `kite_shell.c`:

```c
SHELL_SUBCMD_SET_CREATE(kite_cmds, (kite)); // (1)!
SHELL_CMD_REGISTER(kite, &kite_cmds, "CSSE4011 board commands", NULL); // (2)!
```

1. Creates a mutable subcommand set named `kite_cmds`. The `(kite)` token
   links it to the `kite` namespace -- this is how Zephyr's
   `SHELL_SUBCMD_ADD` knows where to attach subcommands at link time.
2. Registers `kite` as a top-level shell command, pointing to the subcommand
   set.

**2. The `KITE_CMD_ADD` macro** (defined in `kite_shell.h`) wraps Zephyr's
`SHELL_SUBCMD_ADD`:

```c
#define KITE_CMD_ADD(_syntax, _subcmd, _help, _handler) \
    SHELL_SUBCMD_ADD((kite), _syntax, _subcmd, _help, _handler, 0, 0)
```

This lets each subsystem file register itself under `kite` without modifying
`kite_shell.c`. Zephyr collects all `SHELL_SUBCMD_ADD` entries tagged with
`(kite)` at build time and merges them into the `kite_cmds` set automatically.

**3. Each subsystem** defines its own static subcommand set and registers it:

```c
SHELL_STATIC_SUBCMD_SET_CREATE(
    kite_led_cmds,
    SHELL_CMD_ARG(set, NULL, "Set RGB LED state\n...", cmd_led_set, 4, 0),
    SHELL_CMD(off, NULL, "Turn off all LEDs", cmd_led_off),
    SHELL_SUBCMD_SET_END);

KITE_CMD_ADD(led, &kite_led_cmds, "RGB LED commands", NULL);
```

The `SHELL_CMD_ARG` macro's last two arguments are the minimum and maximum
number of *extra* arguments. `cmd_led_set` takes `4` mandatory args (command
name + R + G + B) and `0` optional.

## The Device Tree Overlay

The sample includes a board overlay at
`boards/xiao_ble_nrf52840_sense.overlay` that wires peripherals to the Kite
Shell via aliases:

```dts
/ {
    aliases {
        xiao-d0 = &xiao_d0;  /* GPIO pins D0-D10 */
        ...
        kite-pwm = &pwm1;    /* PWM device */
        kite-uart = &uart0;  /* UART device */
        kite-i2c = &xiao_i2c; /* I2C bus */
    };
};
```

Each subsystem resolves its hardware at compile time through these aliases
using macros like `DT_ALIAS(kite_pwm)` or `GPIO_DT_SPEC_GET(DT_ALIAS(xiao_d0), gpios)`.
This means the overlay is the single source of truth for pin assignments -- if
you need to change which hardware a command talks to, you change the overlay,
not the C code.

## Adding Your Own Command

To add a new subcommand (say, `kite foo`):

1. Create `lib/kite/foo/` with a `Kconfig`, `CMakeLists.txt`, and
   `kite_foo.c`.
2. In `kite_foo.c`, define your handler and register it:
    ```c
    #include "../kite_shell.h"

    static int cmd_foo(const struct shell *sh, size_t argc, char **argv)
    {
        shell_print(sh, "Hello from foo!");
        return 0;
    }

    SHELL_STATIC_SUBCMD_SET_CREATE(
        kite_foo_cmds,
        SHELL_CMD(run, NULL, "Run foo", cmd_foo),
        SHELL_SUBCMD_SET_END);

    KITE_CMD_ADD(foo, &kite_foo_cmds, "Foo commands", NULL);
    ```
3. Add `rsource "foo/Kconfig"` to `lib/kite/Kconfig` and
   `add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_FOO foo)` to
   `lib/kite/CMakeLists.txt`.
4. Enable `CONFIG_CSSE4011_SHELL_FOO=y` in your `prj.conf`.

No changes to `kite_shell.c` are needed -- the linker does the rest.

## Subsystem Reference

Each page below walks through the implementation of one Kite Shell subsystem,
covering the Zephyr APIs it uses, how the device tree configuration works, and
what to look for in the code:

- [LED](led.md) -- onboard RGB LED control via GPIO
- [GPIO](gpio.md) -- general-purpose I/O on the XIAO connector pins
- [PWM](pwm.md) -- pulse-width modulation and LED brightness
- [UART](uart.md) -- serial communication with IRQ and async modes
- [I2C](i2c.md) -- bus scanning and raw read/write
- [ADC](adc.md) -- analog-to-digital conversion via the nRF SAADC
- [IMU](imu.md) -- onboard accelerometer and gyroscope via the sensor API
- [BLE](ble.md) -- non-connectable BLE beacon advertising
