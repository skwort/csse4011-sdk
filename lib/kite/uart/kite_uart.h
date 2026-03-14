/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CSSE4011_KITE_UART_H_
#define CSSE4011_KITE_UART_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define HAS_UART DT_NODE_EXISTS(DT_ALIAS(kite_uart))

#if HAS_UART
extern const struct device *const kite_uart_dev;
#endif

#endif /* CSSE4011_KITE_UART_H_ */
