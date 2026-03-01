/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * USB device setup for the Pico Probe.
 *
 * Derived from zephyr/samples/subsys/usb/common/sample_usbd_init.c with
 * Kconfig references replaced by #define constants.
 *
 * This file builds a USB "device context" -- Zephyr's representation of the
 * entire USB device -- and populates it with:
 *
 *   1. Identity descriptors  (VID, PID, manufacturer/product strings)
 *   2. A configuration       (Full-Speed only; RP2040 has no High-Speed)
 *   3. All USB class instances registered in devicetree / Kconfig
 *      (CMSIS-DAP bulk interface, two CDC-ACM serial ports)
 *   4. The device-descriptor "code triple" required by the USB spec when
 *      Interface Association Descriptors (IAD) are present
 *
 * The caller (main.c) then adds BOS capability descriptors (e.g. MS OS 2.0)
 * and calls usbd_init() + usbd_enable() to go live on the bus.
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/logging/log.h>

#include "usb_init.h"

LOG_MODULE_REGISTER(probe_usb);

/* USB identity -- VID 0x2fe3 is the Zephyr Project vendor ID (dev use only) */
#define PROBE_USB_VID          0x2fe3
#define PROBE_USB_PID          0x0204
#define PROBE_USB_MANUFACTURER "Zephyr Project"
#define PROBE_USB_PRODUCT      "Pico Probe CMSIS-DAP"
#define PROBE_USB_MAX_POWER    250 /* mA */

/* Block DFU mode instance if present */
static const char *const blocklist[] = {
    "dfu_dfu",
    NULL,
};

/*
 * --- Static USB object declarations (compile-time) ---
 *
 * Zephyr's USB device stack uses macros to declare USB objects at compile
 * time. These end up in special linker sections so the stack can find them.
 */

/* The top-level USB device context -- ties everything together */
USBD_DEVICE_DEFINE(probe_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   PROBE_USB_VID, PROBE_USB_PID);

/* Standard string descriptors that the host reads during enumeration */
USBD_DESC_LANG_DEFINE(probe_lang);
USBD_DESC_MANUFACTURER_DEFINE(probe_mfr, PROBE_USB_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(probe_product, PROBE_USB_PRODUCT);

/* Human-readable label for the configuration (visible in USB analysers) */
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

/* Full-Speed configuration descriptor (RP2040 is Full-Speed only, 12 Mbit/s) */
USBD_CONFIGURATION_DEFINE(probe_fs_config, 0, PROBE_USB_MAX_POWER,
                          &fs_cfg_desc);

/*
 * Set the USB device descriptor "code triple":
 *   bDeviceClass / bDeviceSubClass / bDeviceProtocol
 *
 * These three bytes in the device descriptor tell the host what kind of
 * device this is *before* it looks at individual interfaces.
 *
 * - Simple device (one function): set all to 0 -- means "check my
 *   interface descriptors to find out what I do."
 *
 * - Composite device with CDC-ACM (like ours): the CDC-ACM class uses
 *   two USB interfaces per port (communication + data), grouped by an
 *   Interface Association Descriptor (IAD). The USB spec requires that
 *   any device using IADs advertise this specific triple:
 *
 *       bDeviceClass    = 0xEF  (Miscellaneous)
 *       bDeviceSubClass = 0x02  (Common Class)
 *       bDeviceProtocol = 0x01  (Interface Association)
 *
 *   Without this, Windows will fail to bind the CDC-ACM interfaces.
 */
static void probe_fix_code_triple(struct usbd_context *uds_ctx)
{
    if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS)) {
        usbd_device_set_code_triple(uds_ctx, USBD_SPEED_FS,
                                    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    } else {
        usbd_device_set_code_triple(uds_ctx, USBD_SPEED_FS, 0, 0, 0);
    }
}

/*
 * Build the USB device context at runtime.
 *
 * Returns a pointer to the context on success (caller then calls
 * usbd_init + usbd_enable), or NULL on failure.
 */
struct usbd_context *usb_setup_device(void)
{
    int err;

    /*
     * Step 1: Register string descriptors.
     *
     * These are the human-readable strings the host displays when you
     * plug the device in (e.g. "Pico Probe CMSIS-DAP" in Device Manager).
     */
    err = usbd_add_descriptor(&probe_usbd, &probe_lang);
    if (err) {
        LOG_ERR("Failed to add language descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&probe_usbd, &probe_mfr);
    if (err) {
        LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&probe_usbd, &probe_product);
    if (err) {
        LOG_ERR("Failed to add product descriptor (%d)", err);
        return NULL;
    }

    /*
     * Step 2: Add a USB configuration.
     *
     * A USB device can have multiple configurations (e.g. low-power vs
     * high-power), but almost all devices have exactly one. The host
     * selects it during enumeration with SET_CONFIGURATION.
     */
    err = usbd_add_configuration(&probe_usbd, USBD_SPEED_FS, &probe_fs_config);
    if (err) {
        LOG_ERR("Failed to add Full-Speed configuration");
        return NULL;
    }

    /*
     * Step 3: Register all USB class instances.
     *
     * This walks every USB class enabled in Kconfig / devicetree and
     * attaches it to our configuration. For this probe that means:
     *   - CMSIS-DAP bulk interface  (from CONFIG_DAP_BACKEND_USB)
     *   - CDC-ACM uart0             (console logs)
     *   - CDC-ACM uart1             (UART bridge)
     *
     * The blocklist excludes classes we don't want (e.g. DFU mode).
     */
    err = usbd_register_all_classes(&probe_usbd, USBD_SPEED_FS, 1, blocklist);
    if (err) {
        LOG_ERR("Failed to register classes");
        return NULL;
    }

    /*
     * Step 4: Fix up the device descriptor code triple.
     *
     * Must happen after classes are registered so we know whether
     * CDC-ACM (and therefore IADs) are in use.
     */
    probe_fix_code_triple(&probe_usbd);

    return &probe_usbd;
}
