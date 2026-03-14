/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>

#include "../kite_shell.h"
#include "kite_uart.h"

#if HAS_UART
const struct device *const kite_uart_dev = DEVICE_DT_GET(DT_ALIAS(kite_uart));
#endif

SHELL_SUBCMD_SET_CREATE(kite_uart_cmds, (kite, uart));
KITE_CMD_ADD(uart, &kite_uart_cmds, "UART commands", NULL);
