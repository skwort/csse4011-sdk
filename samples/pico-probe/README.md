# Pico Probe CMSIS-DAP

Turns a Raspberry Pi Pico into a CMSIS-DAP v2 debug probe with a USB-UART
bridge, similar to the official Raspberry Pi Debug Probe but running Zephyr.

## Features

- **CMSIS-DAP v2** debug interface over USB bulk (works with OpenOCD)
- **USB-UART bridge** -- CDC-ACM serial port bridged to UART1 (115200 baud)
- **CDC-ACM console** -- second serial port for probe firmware logs
- **MS OS 2.0** descriptors for driverless WinUSB on Windows

## Pin Mapping

| GPIO | Function | Description               |
|------|----------|---------------------------|
| GP1  | nRESET   | Target reset (active low) |
| GP2  | SWCLK    | SWD clock to target       |
| GP3  | SWDIO    | SWD data (pull-up)        |
| GP4  | UART TX  | UART1 TX -> target RX     |
| GP5  | UART RX  | UART1 RX <- target TX     |

## Building and Flashing

```bash
west build -p -b rpi_pico samples/pico-probe
```

Flash via UF2 (hold BOOTSEL, plug in USB):

```bash
west flash --runner uf2
```

Or flash via an existing SWD debug probe:

```bash
west flash --runner openocd # or whatever prove you're using
```

## USB Interfaces

Once running, the probe enumerates as a composite USB device (`2fe3:0204`)
with three interfaces:

| Interface | Type       | Purpose                       |
|-----------|------------|-------------------------------|
| 0         | CMSIS-DAP  | SWD debug (bulk transfers)    |
| 1         | CDC-ACM    | Probe console / logs          |
| 2         | CDC-ACM    | UART bridge (GP4/GP5 <-> USB) |

## Flashing a Target

Use `west flash --runner openocd`. If your board's `board.cmake` doesn't have
CMSIS-DAP OpenOCD runner args configured, you'll need to pass the interface and
target config directly:

```bash
west flash --runner openocd \
  --cmd-pre-init "source [find interface/cmsis-dap.cfg]" \
  --cmd-pre-init "cmsis_dap_vid_pid 0x2fe3 0x0204" \
  --cmd-pre-init "transport select swd" \
  --cmd-pre-init "adapter speed 2000" \
  --cmd-pre-init "source [find target/<your_target>.cfg]"
```

Replace `<your_target>.cfg` with the appropriate OpenOCD target file for your
device (e.g. `nrf52.cfg`, `rp2040.cfg`, `stm32f4x.cfg`).

You can also use OpenOCD directly:

```bash
openocd \
  -f interface/cmsis-dap.cfg \
  -c "cmsis_dap_vid_pid 0x2fe3 0x0204" \
  -c "transport select swd" \
  -c "adapter speed 2000" \
  -f target/<your_target>.cfg \
  -c init
```

## References

Based on the Zephyr DAP sample (`zephyr/samples/subsys/dap`) and the
Raspberry Pi Debug Probe board definition.
