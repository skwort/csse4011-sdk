/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PROBE_USB_INIT_H
#define PROBE_USB_INIT_H

#include <zephyr/usb/usbd.h>

/*
 * Set up and return the USB device context for the probe.
 * Configures descriptors, registers classes, but does NOT
 * call usbd_init() or usbd_enable() - caller does that
 * after adding BOS descriptors.
 */
struct usbd_context *usb_setup_device(void);

#endif /* PROBE_USB_INIT_H */
