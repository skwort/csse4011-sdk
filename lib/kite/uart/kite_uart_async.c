/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include "kite_uart.h"

#if HAS_UART

#define RX_BUF_SIZE 64

static K_SEM_DEFINE(tx_done_sem, 0, 1);
static K_SEM_DEFINE(rx_done_sem, 0, 1);

static uint8_t rx_buf[RX_BUF_SIZE];
static atomic_t rx_pos;

static void async_callback(const struct device *dev, struct uart_event *evt,
                           void *user_data)
{
    switch (evt->type) {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
        k_sem_give(&tx_done_sem);
        break;

    case UART_RX_RDY:
        atomic_set(&rx_pos, evt->data.rx.offset + evt->data.rx.len);
        break;

    case UART_RX_DISABLED:
        k_sem_give(&rx_done_sem);
        break;

    default:
        break;
    }
}

static int async_init(const struct shell *sh)
{
    if (!device_is_ready(kite_uart_dev)) {
        shell_error(sh, "UART device not ready");
        return -ENODEV;
    }

    int ret = uart_callback_set(kite_uart_dev, async_callback, NULL);

    if (ret < 0) {
        shell_error(sh, "Failed to set async callback: %d", ret);
    }
    return ret;
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

/* kite uart send <text> - async/DMA TX */
static int cmd_uart_send(const struct shell *sh, size_t argc, char **argv)
{
    int ret = async_init(sh);

    if (ret < 0) {
        return ret;
    }

    const char *text = argv[1];
    size_t len = strlen(text);

    k_sem_reset(&tx_done_sem);
    ret = uart_tx(kite_uart_dev, text, len, SYS_FOREVER_US);
    if (ret < 0) {
        shell_error(sh, "Async TX failed: %d", ret);
        return ret;
    }

    if (k_sem_take(&tx_done_sem, K_MSEC(5000)) != 0) {
        shell_error(sh, "Async TX timeout");
        return -ETIMEDOUT;
    }

    shell_print(sh, "Sent %u bytes (async)", (uint32_t)len);
    return 0;
}

/* kite uart recv [timeout_ms] - async/DMA RX */
static int cmd_uart_recv(const struct shell *sh, size_t argc, char **argv)
{
    int ret = async_init(sh);

    if (ret < 0) {
        return ret;
    }

    uint32_t timeout_ms = 5000;

    if (argc > 1) {
        timeout_ms = shell_strtoul(argv[1], 0, &ret);
        if (ret < 0) {
            shell_error(sh, "Invalid timeout");
            return -EINVAL;
        }
    }

    atomic_set(&rx_pos, 0);

    k_sem_reset(&rx_done_sem);
    ret = uart_rx_enable(kite_uart_dev, rx_buf, RX_BUF_SIZE, timeout_ms * 1000);
    if (ret < 0) {
        shell_error(sh, "Async RX enable failed: %d", ret);
        return ret;
    }

    shell_print(sh, "Receiving for %u ms (async)...", timeout_ms);

    k_sem_take(&rx_done_sem, K_MSEC(timeout_ms + 1000));

    uint32_t n = atomic_get(&rx_pos);

    print_rx_data(sh, rx_buf, n);
    shell_print(sh, "Received %u bytes (async)", n);
    return 0;
}

/* kite uart loopback <text> - async TX + async RX (wire D6->D7) */
static int cmd_uart_loopback(const struct shell *sh, size_t argc, char **argv)
{
    int ret = async_init(sh);

    if (ret < 0) {
        return ret;
    }

    const char *text = argv[1];
    size_t len = strlen(text);

    if (len > RX_BUF_SIZE) {
        shell_error(sh, "Text too long (max %u)", RX_BUF_SIZE);
        return -EINVAL;
    }

    shell_print(sh, "Loopback: sending %u bytes (wire D6->D7)", (uint32_t)len);

    atomic_set(&rx_pos, 0);

    k_sem_reset(&rx_done_sem);
    ret = uart_rx_enable(kite_uart_dev, rx_buf, RX_BUF_SIZE, 100000);
    if (ret < 0) {
        shell_error(sh, "Async RX enable failed: %d", ret);
        return ret;
    }

    k_sem_reset(&tx_done_sem);
    ret = uart_tx(kite_uart_dev, text, len, SYS_FOREVER_US);
    if (ret < 0) {
        shell_error(sh, "Async TX failed: %d", ret);
        uart_rx_disable(kite_uart_dev);
        k_sem_take(&rx_done_sem, K_MSEC(1000));
        return ret;
    }

    if (k_sem_take(&tx_done_sem, K_MSEC(5000)) != 0) {
        shell_error(sh, "Async TX timeout");
        uart_rx_disable(kite_uart_dev);
        k_sem_take(&rx_done_sem, K_MSEC(1000));
        return -ETIMEDOUT;
    }

    /* RX disables after idle timeout (no next buffer provided) */
    k_sem_take(&rx_done_sem, K_MSEC(2000));

    uint32_t n = atomic_get(&rx_pos);
    uint32_t matched = 0;
    uint32_t failed = 0;

    for (size_t i = 0; i < len; i++) {
        if (i < n && rx_buf[i] == (uint8_t)text[i]) {
            matched++;
        } else {
            shell_warn(sh, "Byte [%u]: sent 0x%02x got 0x%02x", (uint32_t)i,
                       (uint8_t)text[i], i < n ? rx_buf[i] : 0);
            failed++;
        }
    }

    shell_print(sh, "Loopback: %u/%u matched, %u failed", matched,
                (uint32_t)len, failed);
    return 0;
}

/* clang-format off */
SHELL_SUBCMD_ADD((kite, uart), send, NULL,
                 "Send string via async/DMA API\n"
                 "Usage: kite uart send <text>",
                 cmd_uart_send, 2, 0);
SHELL_SUBCMD_ADD((kite, uart), recv, NULL,
                 "Receive via async/DMA API\n"
                 "Usage: kite uart recv [timeout_ms]",
                 cmd_uart_recv, 1, 1);
SHELL_SUBCMD_ADD((kite, uart), loopback, NULL,
                 "Loopback test (wire D6->D7)\n"
                 "Usage: kite uart loopback <text>",
                 cmd_uart_loopback, 2, 0);
/* clang-format on */

#endif /* HAS_UART */
