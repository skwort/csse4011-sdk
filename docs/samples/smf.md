# SMF Traffic Light

This sample demonstrates Zephyr's
[State Machine Framework (SMF)](https://docs.zephyrproject.org/latest/services/smf/index.html)
by implementing a traffic light on the onboard RGB LED. The normal cycle
runs automatically on a timer, and a shell command switches between normal
and flashing-yellow mode.

**Source:** `samples/smf/src/main.c`

## State Machine

```
        Normal Cycle
        ============

    +-------+  3s   +-------+
    |       |------>|       |
    |  RED  |       | GREEN |
    |       |       |       |
    +-------+       +-------+
        ^               |
        | 1s            | 3s
        |   +--------+  |
        |   |        |  |
        +---| YELLOW |<-+
            |        |
            +--------+

        Mode Switch
        ===========

    (any normal state)
            |
            | "smf mode flash"
            v
      +----------+
      | FLASHING |  500ms yellow toggle
      | (yellow) |
      +----------+
            |
            | "smf mode normal"
            v
        +-------+
        |  RED  |  resumes normal cycle
        +-------+
```

## Shell Commands

| Command | Description |
|---------|-------------|
| `smf mode normal` | Run the normal RED -> GREEN -> YELLOW cycle |
| `smf mode flash` | Enter flashing-yellow mode |
| `smf status` | Print current state and mode |

Examples:

```
uart:~$ smf status
State: GREEN  Mode: normal

uart:~$ smf mode flash
Mode set to flash

uart:~$ smf status
State: FLASHING  Mode: flash

uart:~$ smf mode normal
Mode set to normal
```

## Build and Flash

```bash
cd samples/smf
west build -b xiao_ble/nrf52840/sense
west flash
```

## Kconfig

```cfg
CONFIG_SMF=y     # enable state machine framework
CONFIG_GPIO=y    # LED pin control
CONFIG_SHELL=y   # shell commands
CONFIG_LOG=y     # logging output
```

## Code Walkthrough

### Context Struct

SMF requires the user to embed `struct smf_ctx` as the **first member** of a
context object. Additional fields hold the application state:

```c
struct traffic_ctx {
    struct smf_ctx ctx;          // (1)!
    int64_t entered_at;          // (2)!
    enum traffic_mode mode;
    enum traffic_mode requested_mode; // (3)!
    bool flash_on;
};
```

1. Must be the first member -- `SMF_CTX()` casts the outer struct to
   `struct smf_ctx *`.
2. Timestamp recorded on state entry, used by run handlers to check if the
   state's duration has elapsed.
3. The shell writes `requested_mode`; the run handler reads it on the next
   tick. This decoupling avoids any need for synchronisation between the
   shell thread and the main loop.

### State Table

Each state is defined with entry, run, and (optionally) exit handlers. The
`NULL` parent and initial fields mean we are using flat SMF -- no hierarchical
states:

```c
static const struct smf_state states[] = {
    [STATE_RED] =
        SMF_CREATE_STATE(red_entry, red_run, NULL, NULL, NULL),

    [STATE_GREEN] =
        SMF_CREATE_STATE(green_entry, green_run, NULL, NULL, NULL),

    [STATE_YELLOW] =
        SMF_CREATE_STATE(yellow_entry, yellow_run, NULL, NULL, NULL),

    [STATE_FLASHING] =
        SMF_CREATE_STATE(flashing_entry, flashing_run,
                         flashing_exit, NULL, NULL),
};
```

!!! tip
    Indexing the array with the enum values (e.g. `[STATE_RED] = ...`) makes
    transitions readable: `smf_set_state(SMF_CTX(ctx), &states[STATE_GREEN])`.

### State Handlers

Every normal state follows the same pattern:

- **Entry:** record timestamp, set LED colour, log the state name.
- **Run:** check for a mode-change request (transition to FLASHING), then
  check if the duration has elapsed (transition to the next state).

```c
static void red_entry(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->entered_at = k_uptime_get();
    leds_set(1, 0, 0);
    LOG_INF("RED");
}

static enum smf_state_result red_run(void *obj)
{
    struct traffic_ctx *ctx = obj;

    if (ctx->requested_mode == MODE_FLASH) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_FLASHING]);
        return SMF_EVENT_HANDLED;
    }

    if ((k_uptime_get() - ctx->entered_at) >= RED_DURATION_MS) {
        smf_set_state(SMF_CTX(ctx), &states[STATE_GREEN]);
    }

    return SMF_EVENT_HANDLED;
}
```

The FLASHING state is different -- it toggles the yellow LED on each period
and uses an **exit handler** to reset the mode:

```c
static void flashing_exit(void *obj)
{
    struct traffic_ctx *ctx = obj;

    ctx->mode = MODE_NORMAL;
}
```

### Main Loop

The main loop is the simplest possible SMF driver -- call `smf_run_state`
in a loop with a fixed sleep:

```c
smf_set_initial(SMF_CTX(&tl_ctx), &states[STATE_RED]);

while (1) {
    ret = smf_run_state(SMF_CTX(&tl_ctx));
    if (ret != 0) {
        LOG_ERR("State machine terminated: %d", ret);
        leds_off();
        break;
    }
    k_msleep(TICK_MS);
}
```

`smf_set_initial` sets the starting state and runs its entry handler.
Each iteration of the loop calls the current state's run handler, which
may trigger a transition (causing exit → entry of the new state). The
100ms sleep sets the timing resolution for state durations and flash
toggling.

## Try It

1. Build and flash the sample.
2. Observe the LED cycling through red, green, and yellow.
3. Run `smf mode flash` -- the LED should blink yellow.
4. Run `smf status` to confirm the state.
5. Run `smf mode normal` to resume the cycle.
