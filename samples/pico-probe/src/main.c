/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Pico Probe main entry point.
 *
 * Brings up the three subsystems that make this a debug probe:
 *
 *   1. DAP controller  -- CMSIS-DAP command processor, backed by the
 *                         swdp-gpio driver that bit-bangs SWD on GP2/GP3
 *   2. USB device      -- composite device with CMSIS-DAP bulk interface
 *                         + two CDC-ACM serial ports (console & UART bridge)
 *   3. MS OS 2.0 BOS   -- tells Windows to bind WinUSB to the CMSIS-DAP
 *                         interface automatically (no manual driver install)
 *
 * The UART bridge (CDC-ACM <-> UART1) is handled entirely by the
 * "zephyr,uart-bridge" driver instantiated in the devicetree overlay --
 * no code needed here.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <cmsis_dap.h>

/*
 * LOG_MODULE_REGISTER must come before msosv2.h because that header
 * uses LOG_INF in its static callback function. The LOG macros need
 * the log module context (__log_level, etc.) to be declared first.
 */
LOG_MODULE_REGISTER(pico_probe, LOG_LEVEL_INF);

#include "usb_init.h"
#include "msosv2.h"

int main(void)
{
    int ret;

    /*
     * Step 1: Get the SWD debug port device from devicetree.
     *
     * DEVICE_DT_GET_ONE finds the single instance of "zephyr,swdp-gpio"
     * defined in our board overlay (dp0 node). This device drives
     * SWCLK (GP2), SWDIO (GP3), and nRESET (GP6).
     */
    const struct device *const swd_dev = DEVICE_DT_GET_ONE(zephyr_swdp_gpio);

    /*
     * Step 2: Register the SWD device with the DAP subsystem.
     *
     * dap_setup() connects the CMSIS-DAP command processor to our
     * SWD GPIO driver. After this, incoming USB DAP commands will
     * be translated into SWD bus transactions on the physical pins.
     */
    ret = dap_setup(swd_dev);
    if (ret) {
        LOG_ERR("Failed to initialise DAP controller (%d)", ret);
        return ret;
    }

    /*
     * Step 3: Build the USB device context.
     *
     * usb_setup_device() (in usb_init.c) creates the device context
     * with our VID/PID, string descriptors, a Full-Speed configuration,
     * and registers all USB class instances (CMSIS-DAP + 2x CDC-ACM).
     *
     * It returns *without* calling usbd_init/usbd_enable so we can
     * add BOS descriptors first (next step).
     */
    struct usbd_context *usbd = usb_setup_device();
    if (usbd == NULL) {
        LOG_ERR("Failed to set up USB device");
        return -ENODEV;
    }

    /*
     * Step 4: Add the MS OS 2.0 BOS capability descriptor.
     *
     * This is a Binary Object Store (BOS) descriptor that Windows
     * reads during enumeration. It contains:
     *   - A WINUSB compatible ID so Windows loads its generic USB driver
     *   - The CMSIS-DAP v2 device interface GUID so debug tools can
     *     find the device
     *
     * Without this, Windows would require manual driver installation.
     */
    ret = usbd_add_descriptor(usbd, &bos_vreq_msosv2);
    if (ret) {
        LOG_ERR("Failed to add MS OS 2.0 capability descriptor (%d)", ret);
        return ret;
    }

    /*
     * Step 5: Initialise the USB device stack.
     *
     * usbd_init() validates the configuration and prepares the USB
     * controller hardware. After this call, the device descriptor
     * tree is frozen -- no more changes allowed.
     */
    ret = usbd_init(usbd);
    if (ret) {
        LOG_ERR("Failed to initialise USB device (%d)", ret);
        return ret;
    }

    /*
     * Step 6: Enable the USB device.
     *
     * usbd_enable() connects D+/D- pull-up, making the device visible
     * on the bus. The host will enumerate it and bind drivers to each
     * interface (CMSIS-DAP, CDC-ACM console, CDC-ACM UART bridge).
     */
    ret = usbd_enable(usbd);
    if (ret) {
        LOG_ERR("Failed to enable USB device (%d)", ret);
        return ret;
    }

    LOG_INF("Pico Probe ready");

    return 0;
}
