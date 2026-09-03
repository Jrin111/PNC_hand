# SSC tactile hand ROS 2 control

This package is the direct RZ/V2H sensor backend for nine RAA2S4704 devices. It owns one
Linux spidev endpoint and nine GPIO chip-select lines, acquires six I/Q channels per active
device, and exports 54 ROS 2 control sensors. It does not contain the legacy serial, RICBox,
seven-pad, amplitude/contact, or force-conversion paths.

## ROS 2 control contract

The plugin identifier is:

```text
ssc_tactile_hand_ros2_control/SscTactileHandHardwareInterface
```

All 54 sensor names must be declared, from `raa0_ch0` through `raa8_ch5`. Every sensor must
declare exactly these state interfaces:

| Interface | Meaning |
| --- | --- |
| `raw_i` | Unsigned 16-bit RAA I result represented as a `double` |
| `raw_q` | Unsigned 16-bit RAA Q result represented as a `double` |
| `value` | Firmware-compatible EMA of I minus the tare baseline; no force unit is implied |

The transport polls active devices and acquires each device's physical channels in the order
0 through 5. The hardware interface maps them to logical channels with `{5, 0, 1, 2, 3, 4}`
and commits the channel arrays after that polling cycle. Each successfully sampled device
updates all six channels; failed or isolated devices retain their previous values while healthy
devices continue updating. Slots belonging to inactive devices remain zero. The current state
interfaces do not identify retained values as stale, so a new broadcaster timestamp does not
prove that every device was sampled successfully.

Multiple touched regions retain independent values in the same published frame. Polling does
not impose a single-touch restriction, but the samples are taken sequentially rather than at
one simultaneous instant. The configured 40 Hz / 25 ms cycle target still needs measurement on
the assembled hardware.

## Hardware parameters

| Parameter | Default | Validation and use |
| --- | --- | --- |
| `spi_device` | none | Required explicit `/dev/spidevX.Y` path |
| `spi_speed_hz` | `1500000` | 1 through 2,000,000 Hz; the deployed DT node caps at 2 MHz |
| `spi_mode` | `0` | Fixed to mode 0 |
| `bits_per_word` | `8` | Fixed to 8 bits |
| `gpio_chip` | `/dev/gpiochip1` | libgpiod chip path or `gpiochipN` name |
| `cs_lines` | `87,80,56,60,43,68,57,61,62` | Exactly nine unique comma-separated line offsets |
| `active_devices` | `9` | 1 through 9; the first N devices are sampled |
| `channels_per_device` | `6` | Fixed to 6 |
| `auto_tare` | `true` | Runs one tare pass during activation |
| `ema_shift` | `2` | NanoSen fixed-point EMA shift, 0 through 15 |
| `measurement_wait_us` | `145` | Delay between execute and result read |
| `response_check` | `strict` | `strict` rejects echo/CRC mismatches; `log-only` logs one raw write/read response per chip and continues for bring-up |
| `max_consecutive_failures` | `3` | Consecutive acquisition failures before isolating one chip |
| `recovery_interval_frames` | `40` | Frames between attempts to reinitialize an isolated chip |

`spi_device` intentionally has no guessed default. The final spidev node depends on the
deployed device tree and must be confirmed on the target.
All nine configured GPIO lines are requested and held HIGH even when `active_devices` is less
than nine; only the first N lines are ever selected LOW or sampled.

## Confirmed GPIO order

The default line offsets come from the observed post-boot `/dev/gpiochip1` line inventory.
The logical order follows the firmware: four palm RAAs, then five finger RAAs.

| RAA | Board signal | Line offset |
| --- | --- | ---: |
| `raa0` | `PA7` | 87 |
| `raa1` | `PA0` | 80 |
| `raa2` | `P70` | 56 |
| `raa3` | `P74` | 60 |
| `raa4` | `P53` | 43 |
| `raa5` | `P84` | 68 |
| `raa6` | `P71` | 57 |
| `raa7` | `P75` | 61 |
| `raa8` | `P76` | 62 |

`P77`, line offset 63, is the confirmed spare and is not in the active default list.

All requested lines are outputs initialized HIGH. Each register frame drives only the selected
CS LOW, performs one `SPI_IOC_MESSAGE(1)`, and restores that CS HIGH. The invariant starts with
all lines HIGH; a scope guard retries the full all-HIGH operation on every transfer error. The
SPI mode is 0, bytes are MSB-first, and every frame carries a 5 microsecond post-transfer delay
before software releases the GPIO CS.

The driver deliberately does **not** set `SPI_NO_CS`. The spidev controller's native SS output
therefore must be electrically unconnected from the nine RAA CS nets (or otherwise confirmed
safe by the deployed pin configuration); the nine actual RAA selections are the listed GPIO
lines.

## Initialization, tare, and filtering

Activation opens SPI/GPIO resources once and initializes every active RAA with the NanoSen
application unlock, channel-0 measurement configuration, drive-strength, and power sequence.
Every write echo and every response CRC uses the RAA application framing and CRC-16 polynomial
`0x755B`. `response_check=strict` enforces this model. The temporary `log-only` bring-up mode
accepts mismatches while logging complete raw responses once per chip and operation type; it is
intended only to discover the real response format before returning production runs to `strict`.

With `auto_tare=true`, each active physical channel receives five warm-up measurements followed
by exactly 20 measurement attempts. Nonzero I results are averaged; if all 20 I results are
zero, the baseline is zero. Tare also initializes the fixed-point EMA accumulator and the
last-valid sample at the physical index.

Tare stores each baseline under the selected sensor identity. After the five warm-up operations
flush the one-frame pipeline, this is the same logical identity used by normal acquisition after
applying `{5, 0, 1, 2, 3, 4}`. When automatic tare is disabled, activation performs one discarded
channel-5 measurement so the first published frame starts from the same pipeline state.

For a nonzero I sample, `value` uses the NanoSen scaled-integer EMA and updates the last-valid
filtered I. For a zero I sample, it retains the last-valid filtered I. In both cases the exported
invariant is `value = filtered_or_last_valid_i - mapped_tare`; this avoids the firmware's
zero-sample branch accidentally changing the output's meaning.

## Failure and timing behavior

An SPI, echo, CRC, or active-channel acquisition failure restores every requested CS line HIGH,
logs chip/channel context with one-second throttling, and retains the affected chip's last valid
samples. Other chips continue to update. After the configured consecutive-failure threshold the
chip is isolated and retried periodically; recovery reinitializes it and repeats tare or pipeline
priming. A transient mid-frame failure also triggers a discarded channel-5 measurement before the
next acquisition so the one-frame channel pipeline cannot resume from a misaligned state. Failure
to restore all CS lines HIGH remains a fatal safety error.

Full-frame acquisitions are monitored continuously in 100-frame windows. Average and maximum
times are logged for each window. Exceeding the nominal 25 ms / 40 Hz budget emits a warning but
does not stop acquisition; the most recently supplied positive ROS 2 control `read()` period is
included for diagnosis.

## Target prerequisites and open hardware questions

The package targets the libgpiod 1.x C API provided by Ubuntu Noble's `libgpiod-dev` 1.6.3
metadata. It uses `gpiod_chip_open`, per-line requests initially HIGH, `gpiod_line_set_value`,
line release, and chip close. The CMake configuration rejects libgpiod 2.x so an API migration
cannot happen silently. The inspected target currently has no libgpiod runtime or CLI installed;
the matching Noble runtime and development/sysroot packages are prerequisites for building and
running this package.

Hardware validation still requires CRC/echo testing on every populated RAA, sustained 40 Hz
timing, and the physical mapping from 54 raw electrical slots to the documented 47 tactile zones.
Historical deployment evidence exists for the earlier build, but the revised response and
recovery behavior remains unverified on the target until a new build is deployed.
