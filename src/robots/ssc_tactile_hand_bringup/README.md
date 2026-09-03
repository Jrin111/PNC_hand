# ssc_tactile_hand_bringup

Minimal ROS 2 Jazzy bringup for the WS125 RZ/V2H-direct PNC tactile hand. It
starts a namespaced `robot_state_publisher`, `ros2_control_node`, and
`StateInterfacesBroadcaster`; there is no mock device, GUI, or calibration utility.

The hardware topology is one shared SPI6 userspace device plus nine independent
GPIO chip-select lines. The package exports 54 generic sensors,
`raa0_ch0` through `raa8_ch5`, with three state interfaces per sensor:
`raw_i`, `raw_q`, and `value` (162 interfaces total).

## Configured default device mapping

These defaults follow the WS125/RZ/V2H device-tree and tactile-board wiring:

| Device | Physical signal | GPIO device | Line offset |
|---|---|---|---:|
| `raa0` | Palm 1 / PA7 | `/dev/gpiochip1` | 87 |
| `raa1` | Palm 2 / PA0 | `/dev/gpiochip1` | 80 |
| `raa2` | Palm 3 / P70 | `/dev/gpiochip1` | 56 |
| `raa3` | Palm 4 / P74 | `/dev/gpiochip1` | 60 |
| `raa4` | Finger 1 / P53 | `/dev/gpiochip1` | 43 |
| `raa5` | Finger 2 / P84 | `/dev/gpiochip1` | 68 |
| `raa6` | Finger 3 / P71 | `/dev/gpiochip1` | 57 |
| `raa7` | Finger 4 / P75 | `/dev/gpiochip1` | 61 |
| `raa8` | Finger 5 / P76 | `/dev/gpiochip1` | 62 |

The shared SPI node is `/dev/spidev1.0`. P77 / line 63 is the spare Palm 5
chip select and is intentionally not in the active list. Keep `cs_lines`
ordered by logical device index; do not rely on GPIO enumeration order.
Re-confirm this mapping after changing the DTB or target image.

## Configuration

`config/tactile_hand.yaml` supplies launch defaults. The launch file injects
every hardware setting into the generated ros2_control URDF as
`<hardware><param>` entries; they are not passed only as node parameters.

| Launch argument | Default |
|---|---|
| `spi_device` | `/dev/spidev1.0` |
| `spi_speed_hz` | `1500000` |
| `spi_mode` | `0` |
| `bits_per_word` | `8` |
| `gpio_chip` | `/dev/gpiochip1` |
| `cs_lines` | `87,80,56,60,43,68,57,61,62` |
| `active_devices` | `9` |
| `channels_per_device` | `6` |
| `auto_tare` | `true` |
| `ema_shift` | `2` |
| `measurement_wait_us` | `145` |
| `response_check` | `strict` |
| `max_consecutive_failures` | `3` |
| `recovery_interval_frames` | `40` |
| `update_rate` | `40` |
| `namespace` | `tactile` |

The controller manager starts as `/tactile/controller_manager` at 40 Hz, avoiding the root
`/controller_manager` used by the RH56 stack and giving a 25 ms nominal read period. The
namespace can be overridden at launch; parameter YAML uses ROS wildcard node names so it follows
the selected namespace.
`update_rate` overrides the controller-manager parameter and is intentionally
not a hardware xacro parameter.

## Build and launch

From the RZ/V2H xbuild container:

```bash
cd /home/ubuntu/ros2_ws
sysroot-rosdep-install
cross-colcon-build --packages-up-to ssc_tactile_hand_bringup
```

The workspace includes the official Jazzy `state_interfaces_broadcaster` 4.42.1 source under
`src/utils`, pinned by its provenance file. Building `--packages-up-to` therefore installs the
required AArch64 controller plugin together with the two tactile packages instead of relying on
an undeclared board-side overlay.

After deploying the install tree and confirming access to the SPI/GPIO devices:

```bash
ros2 launch ssc_tactile_hand_bringup ssc_tactile_hand.launch.py
```

All hardware parameters can be overridden at launch and are forwarded through
xacro, for example a one-device check:

```bash
ros2 launch ssc_tactile_hand_bringup ssc_tactile_hand.launch.py \
  active_devices:=1 auto_tare:=true
```

The complete nine-line `cs_lines` mapping remains configured during a
single-device check. The hardware plugin requests and holds all nine lines
HIGH, while only the first `active_devices` entries can be selected and sampled.

For the single-chip response-format experiment, use `response_check:=log-only` and inspect the
one-time raw write/read response warnings. Return to the default `strict` mode after the hardware
response format is confirmed.

The default namespace makes the broadcaster publish the explicit interface names and values on:

- `/tactile/tactile_hand_state_broadcaster/names`
- `/tactile/tactile_hand_state_broadcaster/values`

## Files

- `config/tactile_hand.yaml`: ordered hardware defaults.
- `config/controller_manager.yaml`: 40 Hz controller manager and all 162
  explicit broadcaster interfaces.
- `urdf/ssc_tactile_hand.urdf.xacro`: hardware parameters and 54 sensor blocks.
- `launch/ssc_tactile_hand.launch.py`: xacro generation, validation, and startup.

## License

Apache-2.0
