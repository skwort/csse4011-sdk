# UART

The `kite uart` command provides serial communication over a secondary UART
(separate from the shell's console UART). It supports two mutually exclusive
API modes -- interrupt-driven (IRQ) and async/DMA -- selectable via Kconfig.
This lets you explore both programming models side by side.

**Source:** `lib/kite/uart/kite_uart.c`, `kite_uart_irq.c`, `kite_uart_async.c`

!!! note
    This is the most complex subsystem in Kite Shell. If you're new to Zephyr,
    start with [LED](led.md) or [GPIO](gpio.md) first -- they cover the core
    patterns (device tree specs, shell handlers, Kconfig) without the added
    complexity of interrupts and callbacks.

## Commands

Both modes share the same command names, so the user experience is identical
regardless of which backend is active:

| Command | Description |
|---------|-------------|
| `kite uart send <text>` | Transmit a string |
| `kite uart recv [timeout_ms]` | Receive data (default timeout: 1000 ms IRQ / 5000 ms async) |
| `kite uart loopback <text>` | Loopback test -- requires D6 wired to D7 |

The IRQ mode additionally provides:

| Command | Description |
|---------|-------------|
| `kite uart irq <start\|stop>` | Start/stop background interrupt-driven receive |

Examples:

```
uart:~$ kite uart send hello
Sent 5 bytes (polling)

uart:~$ kite uart loopback test
Loopback: sending 4 bytes (wire D6->D7)
Loopback: 4/4 matched, 0 failed
```

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_UART=y  # default y, selects SERIAL
```

The API mode is a Kconfig `choice` -- exactly one must be active:

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

To switch to async mode, add to your `prj.conf` or use the provided
`prj_uart_async.conf`:

```cfg
CONFIG_CSSE4011_SHELL_UART_ASYNC=y
CONFIG_UART_0_INTERRUPT_DRIVEN=n
```

!!! warning
    `UART_0_INTERRUPT_DRIVEN=n` is required because the Zephyr shell console
    defaults to interrupt-driven mode on UART0. Since `kite-uart` is aliased
    to `uart0`, you must disable interrupt-driven mode to enable the async
    API on the same peripheral.

## Architecture

The UART subsystem is split across three files:

- **`kite_uart.c`** -- creates the `kite uart` subcommand set and instantiates
  the shared device pointer.
- **`kite_uart_irq.c`** -- IRQ-mode command implementations (compiled when
  `CONFIG_CSSE4011_SHELL_UART_IRQ=y`).
- **`kite_uart_async.c`** -- async-mode command implementations (compiled when
  `CONFIG_CSSE4011_SHELL_UART_ASYNC=y`).

The shared header (`kite_uart.h`) exports the device pointer so both backends
can use it:

```c
#define HAS_UART DT_NODE_EXISTS(DT_ALIAS(kite_uart))

#if HAS_UART
extern const struct device *const kite_uart_dev;
#endif
```

Unlike the other subsystems that use `SHELL_STATIC_SUBCMD_SET_CREATE`, the
UART subsystem uses a *dynamic* subcommand set via `SHELL_SUBCMD_SET_CREATE`.
This is because the commands are registered from separate compilation units
(the IRQ and async files) using `SHELL_SUBCMD_ADD` -- the same distributed
registration pattern that `KITE_CMD_ADD` uses for the top-level `kite`
command.

## Code Walkthrough: IRQ Mode

### Interrupt Handler and Ring Buffer

When `kite uart irq start` is called, an interrupt handler is installed that
fills a 64-byte buffer as data arrives:

```c
static uint8_t rx_buf[RX_BUF_SIZE];
static atomic_t rx_len; // (1)!

static void uart_irq_handler(const struct device *dev, void *user_data)
{
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint32_t pos = atomic_get(&rx_len);
            int n = uart_fifo_read(dev, &rx_buf[pos], RX_BUF_SIZE - pos); // (2)!

            if (n > 0) {
                atomic_set(&rx_len, pos + n);
            }
        }
    }
}
```

1. `atomic_t` because `rx_len` is written from interrupt context and read
   from the shell thread.
2. `uart_fifo_read` reads whatever bytes are available in the hardware FIFO
   into our buffer starting at `pos`. It returns the number of bytes
   actually read.

When `kite uart irq stop` is called, the interrupt is disabled and the
accumulated data is printed:

```c
uart_irq_rx_disable(kite_uart_dev);
uint32_t n = atomic_get(&rx_len);
print_rx_data(sh, rx_buf, n);
```

### Polling Loopback Test

The IRQ-mode loopback test uses **polling** (not interrupts) for both TX and
RX, sending one byte at a time and waiting up to 100 ms for each echo:

```c
for (size_t i = 0; i < len; i++) {
    uart_poll_out(kite_uart_dev, text[i]);

    int64_t deadline = k_uptime_get() + 100;

    while (k_uptime_get() < deadline) {
        if (uart_poll_in(kite_uart_dev, &c) == 0) {
            if (c == (uint8_t)text[i]) {
                matched++;
            }
            ...
            break;
        }
        k_sleep(K_MSEC(1));
    }
}
```

This is intentionally simple -- it demonstrates the polling API and verifies
the physical wiring without the complexity of interrupts.

## Code Walkthrough: Async Mode

### Event-Driven Callbacks

The async mode uses Zephyr's `uart_callback_set` API. A single callback
handles all UART events:

```c
static K_SEM_DEFINE(tx_done_sem, 0, 1); // (1)!
static K_SEM_DEFINE(rx_done_sem, 0, 1);
static uint8_t rx_buf[RX_BUF_SIZE];
static atomic_t rx_pos;

static void async_callback(const struct device *dev, struct uart_event *evt,
                            void *user_data)
{
    switch (evt->type) {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
        k_sem_give(&tx_done_sem); // (2)!
        break;

    case UART_RX_RDY:
        atomic_set(&rx_pos, evt->data.rx.offset + evt->data.rx.len); // (3)!
        break;

    case UART_RX_DISABLED:
        k_sem_give(&rx_done_sem);
        break;
    ...
    }
}
```

1. Semaphores synchronise the shell thread with the async callback. The
   shell thread blocks on `k_sem_take`, and the callback releases the
   semaphore when the operation completes.
2. Both `TX_DONE` and `TX_ABORTED` release the semaphore so the caller
   doesn't block forever on error.
3. `rx_pos` tracks how many bytes have been received. `atomic_t` is used
   instead of `volatile` because the async callback may run on a different
   context (ISR or DMA completion handler).

### Async Send

```c
k_sem_reset(&tx_done_sem);
ret = uart_tx(kite_uart_dev, text, len, SYS_FOREVER_US); // (1)!
...
k_sem_take(&tx_done_sem, K_MSEC(5000)); // (2)!
```

1. `uart_tx` initiates an asynchronous transmit. The driver takes ownership
   of the buffer until the `UART_TX_DONE` event fires.
2. The shell thread blocks until TX completes or 5 seconds elapse.

### Async Loopback

The async loopback is more complex -- it starts RX *before* TX to ensure
the receive buffer is ready to catch the echoed data:

```c
ret = uart_rx_enable(kite_uart_dev, rx_buf, RX_BUF_SIZE, 100000); // (1)!
...
ret = uart_tx(kite_uart_dev, text, len, SYS_FOREVER_US);
...
k_sem_take(&tx_done_sem, K_MSEC(5000));
k_sem_take(&rx_done_sem, K_MSEC(2000)); // (2)!
```

1. `100000` is the idle timeout in microseconds (100 ms). After the last
   byte is received, if no more data arrives within 100 ms, the driver
   fires `UART_RX_DISABLED` and the RX stops automatically. This is how
   the async API knows "the transfer is done" without knowing the length
   in advance.
2. Wait for `UART_RX_DISABLED` which signals that the idle timeout has
   expired and all data has been received.

## Try It

Wire D6 (TX) to D7 (RX) on your XIAO BLE with a jumper wire, then:

```
uart:~$ kite uart loopback hello
Loopback: sending 5 bytes (wire D6->D7)
Loopback: 5/5 matched, 0 failed
```

If you get timeouts or mismatches, check the wiring and make sure no other
peripheral is using D6/D7.
