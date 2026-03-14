# LED

The `kite led` command controls the onboard RGB LED on the XIAO BLE Sense
board. Each colour channel (red, green, blue) is a separate GPIO pin, so this
module uses Zephyr's GPIO driver -- not a dedicated LED driver.

**Source:** `lib/kite/led/kite_led.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite led set <r> <g> <b>` | Set each channel on (1) or off (0) |
| `kite led off` | Turn off all channels and stop any active blink |
| `kite led blink <color> <period_ms>` | Blink a single channel at the given period |

Examples:

```
uart:~$ kite led set 1 0 0
LED: R=1 G=0 B=0

uart:~$ kite led blink g 500
Blinking green at 500 ms

uart:~$ kite led off
LEDs off
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_LED=y  # default y when CSSE4011_SHELL is enabled
```

This selects the `GPIO` driver automatically.

## Code Walkthrough

### Pin Setup

The three LED channels are defined using device tree aliases `led0`, `led1`,
and `led2`, which are already provided by the XIAO BLE board definition (not
the kite overlay):

```c
#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);
```

`GPIO_DT_SPEC_GET` pulls the port, pin number, and active-level flags directly
from the device tree at compile time, bundling them into a `gpio_dt_spec`
struct. All subsequent GPIO calls use this spec, so the C code never contains
raw pin numbers.

### Lazy Initialisation

The LED pins are configured the first time `kite led set` is called, not at
boot:

```c
static int cmd_led_set(const struct shell *sh, size_t argc, char **argv)
{
    static bool initialised;

    if (!initialised) {
        ret = led_init(); // (1)!
        if (ret < 0) {
            shell_error(sh, "LED init failed: %d", ret);
            return ret;
        }
        initialised = true;
    }
    ...
}
```

1. `led_init()` calls `gpio_pin_configure_dt()` on each channel with
   `GPIO_OUTPUT_INACTIVE`, which sets the pin as an output driven to the
   inactive level (LED off, respecting the active-low/active-high flag from
   the device tree).

This pattern avoids claiming the GPIO pins at boot when the user may not need
the LED subsystem.

### Blink with Delayable Work

The `blink` command uses Zephyr's work queue system rather than a dedicated
thread or a busy-wait loop:

```c
static struct k_work_delayable blink_work;
static struct gpio_dt_spec const *blink_led;
static uint32_t blink_period_ms;
static bool blink_active;

static void blink_work_handler(struct k_work *work)
{
    if (!blink_active || blink_led == NULL) {
        return; // (1)!
    }

    gpio_pin_toggle_dt(blink_led); // (2)!
    k_work_reschedule(&blink_work, K_MSEC(blink_period_ms)); // (3)!
}
```

1. Guard against a race where `blink_active` was set to `false` (by
   `kite led off`) but the work item had already been submitted.
2. `gpio_pin_toggle_dt` flips the pin between active and inactive, taking
   the active-level flag into account.
3. `k_work_reschedule` resubmits the work item to the system work queue
   after `blink_period_ms` milliseconds. This creates a self-rescheduling
   loop without blocking any thread.

`k_work_delayable` is the idiomatic way to do periodic tasks in Zephyr
without spinning up a thread. The work runs on the system work queue thread,
so it doesn't need its own stack allocation. The tradeoff is that if other
work items are queued, there may be slight jitter in the blink timing -- fine
for an LED, but not suitable for hard real-time tasks.

### Stopping a Blink

When `kite led off` is called, it cancels the pending work and clears all
channels:

```c
if (blink_active) {
    blink_active = false;
    k_work_cancel_delayable(&blink_work);
}

gpio_pin_set_dt(&led_r, 0);
gpio_pin_set_dt(&led_g, 0);
gpio_pin_set_dt(&led_b, 0);
```

Setting `blink_active = false` *before* cancelling ensures the handler is a
no-op even if it fires between the flag set and the cancel call.
