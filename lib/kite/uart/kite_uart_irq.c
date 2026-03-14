/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "kite_uart.h"

#if HAS_UART

#define RX_BUF_SIZE 64

static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_len;

static void uart_irq_handler(const struct device *dev, void *user_data)
{
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint32_t pos = rx_len;
            int n = uart_fifo_read(dev, &rx_buf[pos], RX_BUF_SIZE - pos);

            if (n > 0) {
                rx_len = pos + n;
            }
        }
    }
}

static void print_rx_data(const struct shell *sh, const uint8_t *buf,
                          uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = buf[i];

        if (c >= 0x20 && c < 0x7f) {
            shell_print(sh, "rx: 0x%02x '%c'", c, c);
        } else {
            shell_print(sh, "rx: 0x%02x", c);
        }
    }
}

/* kite uart send <text> - polling TX */
static int cmd_uart_send(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(kite_uart_dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    const char *text = argv[1];
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(kite_uart_dev, text[i]);
    }

    shell_print(sh, "Sent %u bytes (polling)", (uint32_t)len);
    return 0;
}

/* kite uart recv [timeout_ms] - polling RX */
static int cmd_uart_recv(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(kite_uart_dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    uint32_t timeout_ms = 1000;

    if (argc > 1) {
        int ret;

        timeout_ms = shell_strtoul(argv[1], 0, &ret);
        if (ret < 0) {
            shell_error(sh, "Invalid timeout");
            return -EINVAL;
        }
    }

    shell_print(sh, "Polling for %u ms...", timeout_ms);

    int64_t end = k_uptime_get() + timeout_ms;
    uint32_t count = 0;
    uint8_t c;

    while (k_uptime_get() < end) {
        if (uart_poll_in(kite_uart_dev, &c) == 0) {
            if (c >= 0x20 && c < 0x7f) {
                shell_print(sh, "rx: 0x%02x '%c'", c, c);
            } else {
                shell_print(sh, "rx: 0x%02x", c);
            }
            count++;
        }
        k_sleep(K_MSEC(1));
    }

    shell_print(sh, "Received %u bytes", count);
    return 0;
}

/* kite uart irq <start|stop> - interrupt-driven background RX */
static int cmd_uart_irq(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(kite_uart_dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    if (strcmp(argv[1], "start") == 0) {
        rx_len = 0;
        uart_irq_callback_set(kite_uart_dev, uart_irq_handler);
        uart_irq_rx_enable(kite_uart_dev);
        shell_print(sh, "IRQ receive started");
    } else if (strcmp(argv[1], "stop") == 0) {
        uart_irq_rx_disable(kite_uart_dev);
        uint32_t n = rx_len;

        print_rx_data(sh, rx_buf, n);
        shell_print(sh, "IRQ receive stopped (%u bytes)", n);
    } else {
        shell_error(sh, "Usage: kite uart irq <start|stop>");
        return -EINVAL;
    }

    return 0;
}

/* kite uart loopback <text> - polling TX + polling RX (wire D6->D7) */
static int cmd_uart_loopback(const struct shell *sh, size_t argc, char **argv)
{
    if (!device_is_ready(kite_uart_dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    const char *text = argv[1];
    size_t len = strlen(text);
    uint32_t matched = 0;
    uint32_t failed = 0;
    uint8_t c;

    shell_print(sh, "Loopback: sending %u bytes (wire D6->D7)", (uint32_t)len);

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(kite_uart_dev, text[i]);

        int64_t deadline = k_uptime_get() + 100;
        bool got = false;

        while (k_uptime_get() < deadline) {
            if (uart_poll_in(kite_uart_dev, &c) == 0) {
                if (c == (uint8_t)text[i]) {
                    matched++;
                } else {
                    shell_warn(sh, "Mismatch [%u]: sent 0x%02x got 0x%02x",
                               (uint32_t)i, (uint8_t)text[i], c);
                    failed++;
                }
                got = true;
                break;
            }
            k_sleep(K_MSEC(1));
        }

        if (!got) {
            shell_warn(sh, "Timeout [%u]: no response for 0x%02x", (uint32_t)i,
                       (uint8_t)text[i]);
            failed++;
        }
    }

    shell_print(sh, "Loopback: %u/%u matched, %u failed", matched,
                (uint32_t)len, failed);
    return 0;
}

/* clang-format off */
SHELL_SUBCMD_ADD((kite, uart), send, NULL,
                 "Send string via polling API\n"
                 "Usage: kite uart send <text>",
                 cmd_uart_send, 2, 0);
SHELL_SUBCMD_ADD((kite, uart), recv, NULL,
                 "Receive via polling API\n"
                 "Usage: kite uart recv [timeout_ms]",
                 cmd_uart_recv, 1, 1);
SHELL_SUBCMD_ADD((kite, uart), irq, NULL,
                 "Interrupt-driven receive\n"
                 "Usage: kite uart irq <start|stop>",
                 cmd_uart_irq, 2, 0);
SHELL_SUBCMD_ADD((kite, uart), loopback, NULL,
                 "Loopback test (wire D6->D7)\n"
                 "Usage: kite uart loopback <text>",
                 cmd_uart_loopback, 2, 0);
/* clang-format on */

#endif /* HAS_UART */
