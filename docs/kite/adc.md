# ADC

The `kite adc` command reads analog voltages from the XIAO BLE connector pins
using the nRF52840's SAADC (Successive Approximation Analog-to-Digital
Converter). It reports both the raw 12-bit sample value and the converted
millivolt reading.

**Source:** `lib/kite/adc/kite_adc.c`

## Commands

| Command | Description |
|---------|-------------|
| `kite adc read <pin>` | Read a single analog pin (D0-D3) |
| `kite adc read_all` | Read all four analog pins |

Examples:

```
uart:~$ kite adc read D0
D0: 2048 raw, 1800 mV

uart:~$ kite adc read_all
D0: 2048 raw, 1800 mV
D1:    0 raw,    0 mV
D2: 4095 raw, 3600 mV
D3: 1024 raw,  900 mV
```

## Pin Mapping

The XIAO BLE has four analog-capable pins exposed on the connector. The
mapping from connector pin to nRF SAADC analog input is defined in the device
tree overlay:

| Connector Pin | nRF Pin | SAADC Channel | Overlay Node |
|---------------|---------|---------------|--------------|
| D0 | P0.02 | AIN0 | `channel@0` |
| D1 | P0.03 | AIN1 | `channel@1` |
| D2 | P0.28 | AIN4 | `channel@2` |
| D3 | P0.29 | AIN5 | `channel@3` |

Note that the SAADC AIN numbers don't match the channel indices -- D2 is AIN4,
not AIN2. This is because the AIN assignment is fixed by the nRF52840 silicon
(each physical pin is hardwired to a specific AIN), while the channel indices
are just sequential labels in our overlay.

## Kconfig

```cfg
CONFIG_CSSE4011_SHELL_ADC=y  # default y
```

Selects the `ADC` driver.

## Device Tree Configuration

Each ADC channel is configured in the overlay with identical settings:

```dts
&adc {
    channel@0 {
        reg = <0>;
        zephyr,gain = "ADC_GAIN_1_6";        /* (1)! */
        zephyr,reference = "ADC_REF_INTERNAL"; /* (2)! */
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,input-positive = <NRF_SAADC_AIN0>;
        zephyr,resolution = <12>;             /* (3)! */
    };
    ...
};
```

1. **Gain 1/6** -- the SAADC input is attenuated by a factor of 6. Combined
   with the 0.6 V internal reference, this gives a full-scale input range of
   0.6 V x 6 = **3.6 V**, which comfortably covers the 3.3 V supply rail.
2. **Internal reference** -- the nRF52840 has a fixed 0.6 V internal reference.
   No external reference voltage is needed.
3. **12-bit resolution** -- raw values range from 0 to 4095.

The channels are made available to the C code via the `zephyr,user` node:

```dts
zephyr,user {
    io-channels = <&adc 0>, <&adc 1>, <&adc 2>, <&adc 3>;
};
```

## Code Walkthrough

### Building the Channel Table

The code uses a device tree foreach macro to build a compile-time array of
ADC channel specs:

```c
#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
                         DT_SPEC_AND_COMMA)
};
```

`DT_FOREACH_PROP_ELEM` iterates over each entry in the `io-channels` property
of the `zephyr,user` node. For each entry, `ADC_DT_SPEC_GET_BY_IDX` extracts
the ADC device, channel ID, and all the configuration (gain, reference,
resolution) into an `adc_dt_spec` struct. The result is a four-element array
where `adc_channels[0]` corresponds to D0, `adc_channels[1]` to D1, and so on.

### Lazy Channel Setup

Channels are configured on first use rather than at boot:

```c
static bool channels_configured;

static int ensure_channels_configured(const struct shell *sh)
{
    if (channels_configured) {
        return 0;
    }

    for (size_t i = 0; i < NUM_ADC_CHANNELS; i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            shell_error(sh, "ADC device not ready");
            return -ENODEV;
        }

        int ret = adc_channel_setup_dt(&adc_channels[i]); // (1)!
        if (ret < 0) {
            shell_error(sh, "Channel %zu setup failed: %d", i, ret);
            return ret;
        }
    }

    channels_configured = true;
    return 0;
}
```

1. `adc_channel_setup_dt` programs the SAADC hardware with the gain,
   reference, acquisition time, and input pin for this channel. This only
   needs to happen once -- the configuration persists until the device is
   reset.

### Taking a Reading

Each reading goes through three steps:

```c
static int read_channel(const struct shell *sh, size_t idx,
                        int16_t *raw, int32_t *mv)
{
    uint16_t buf;
    struct adc_sequence seq = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    adc_sequence_init_dt(&adc_channels[idx], &seq); // (1)!

    int ret = adc_read_dt(&adc_channels[idx], &seq); // (2)!
    if (ret < 0) {
        return ret;
    }

    *raw = (int16_t)buf;
    *mv = (int32_t)buf;
    adc_raw_to_millivolts_dt(&adc_channels[idx], mv); // (3)!

    return 0;
}
```

1. **Initialise the sequence** -- `adc_sequence_init_dt` fills in the
   channel mask and resolution from the DT spec. The `adc_sequence` struct
   tells the driver where to store the result and which channels to sample.
2. **Trigger the conversion** -- `adc_read_dt` starts the SAADC, waits for
   the conversion to complete, and writes the raw value into `buf`. This is
   a blocking call.
3. **Convert to millivolts** -- `adc_raw_to_millivolts_dt` applies the gain
   and reference voltage to convert the raw 12-bit value to a physical
   voltage. With gain 1/6 and a 0.6 V reference, the formula is:
   `mV = raw * 3600 / 4095`.

### Pin Name Parsing

Like the GPIO subsystem, the ADC module parses `D`-prefixed pin names:

```c
static int parse_analog_pin(const char *name)
{
    if ((name[0] == 'D' || name[0] == 'd') && name[1] != '\0') {
        char *end;
        long idx = strtol(&name[1], &end, 10);

        if (*end == '\0' && idx >= 0 && idx < (long)NUM_ADC_CHANNELS) {
            return (int)idx;
        }
    }
    return -1;
}
```

The valid range is D0-D3 (four analog channels). Requesting D4 or higher
returns an error.
