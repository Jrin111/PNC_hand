# PNC tactile and Inspire hand panels

This Foxglove extension keeps the Inspire Console, Force and Gripper command panels and replaces the six-pad SSC contact display with **PNC Tactile Diagnostics**. It shows all 54 electrical channels, a selected channel's raw I/Q and relative pressure, and an explicitly enabled simulation input. The 3D robot surface colors are produced separately by the PNC ROS visualization node; this extension does not guess anatomical zone positions.

## Source and license

Adapted from `https://partnergitlab.renesas.solutions/pai/ros2/utility/foxglove/foxglove_inspire_hand_panels`, source commit `a3976ebb68718e7184eb16c3a5f255735f866130`, copied from the workspace reference repository. Original Apache-2.0 LICENSE and publisher identity are retained. Version 1.1.0 adds the PNC adapter, diagnostics, simulation lease and ROS 2 publish definitions. Original `.git` and `node_modules` were not copied.

## Panels

Version 1.1.1 fixes the Console layout in short panels: joint rows keep a readable minimum height and the panel scrolls vertically.

- **Dexterous Hand Console**: six joint targets, presets and live position feedback.
- **Dexterous Hand Force**: force thresholds and measured feedback when provided. Position-only simulation has **Unknown** force/velocity; no force is fabricated.
- **Dexterous Hand Gripper**: width command through `/gripper_command`.
- **PNC Tactile Diagnostics**: 9 banks of 6 channels, raw I/Q for the selected channel, relative pressure, stream freshness, and simulator-only injection.

The retained actuator controls still publish commands to the connected ROS domain. The launch configuration isolates simulation into its own ROS_DOMAIN_ID; the extension does not change ROS domains.

## PNC topics and decoding

| Direction | Topic | ROS type / schema |
| --- | --- | --- |
| Subscribe | `/tactile/tactile_hand_state_broadcaster/names` | `control_msgs/msg/Keys` |
| Subscribe | `/tactile/tactile_hand_state_broadcaster/values` | `control_msgs/msg/Float64Values` |
| Subscribe | `/pnc_demo/enabled` | `std_msgs/msg/Bool` simulator heartbeat |
| Converted topic | `/pnc/tactile_channels` | `renesas.PncTactileChannels` |
| Publish, only when explicitly enabled | `/pnc_demo/tactile_values` | `std_msgs/msg/Float64MultiArray`, exactly 54 values |

Keys must be `raa0_ch0/raw_i`, `raa0_ch0/raw_q`, `raa0_ch0/value` through `raa8_ch5/...`. The adapter matches names, never assumes the broadcaster's array order. Values received before names, missing/duplicate keys, short arrays, nonnumeric values, NaN and infinity remain **Unknown**, represented as NaN plus per-field `*_known=false` in the converter. A measured zero remains a known zero. Changing the names map invalidates the preceding frame.

The converted topic contains `{ names_received, channels: [{ name, raw_i, raw_q, value, raw_i_known, raw_q_known, value_known }] }`. Example Plot path:

```text
/pnc/tactile_channels.channels[:]{name=="raa4_ch2"}.value
```

`value` is relative `EMA(I) - tare`, not Newtons. Diagnostics use the same piecewise RGB interpolation and five color stops as `pnc_tactile_visualizer/core.py:response_color`, over 0..1 (blue/cyan/green/yellow/red); out-of-range values are color-clamped while their numeric readings remain unchanged. Gray means unknown. The 54 electrical slots are not a verified mapping to 47 physical zones.

## Freshness and simulation

**Live stream** means a values message arrived within two seconds. It does not claim an individual chip is online: the hardware backend may retain old samples without emitting a quality indicator. On seek, unavailable topics, explicit **Pause / clear**, or a two-second values timeout, the panel clears displayed data and simulation authorization. A timeout/resume requests the transient-local names topic again. Foxglove's current extension API has no playback-pause boolean; a paused player is therefore detected by the bounded no-message timeout. The per-subscriber converter also clears its name map when message time moves backwards.

Simulation is a separate launch/source mode. The simulator must publish `/pnc_demo/enabled=true` continuously (1 Hz or faster). Injection requires all of:

1. A literal true heartbeat received less than two seconds ago.
2. A fresh tactile values stream and a connection that supports publishing.
3. The operator explicitly checking **Enable simulation input** in this panel.

The checkbox defaults off, is never persisted, and is cleared when authorization expires. Every publish re-checks the heartbeat and stream timestamps. No injection is sent from mounting, reconnecting, or restoring a layout. The selected channel slider changes one entry of a 54-value relative-pressure vector in `chip * 6 + channel` order; the other entries preserve the panel's current simulation draft. **Clear all 54 simulation channels** sends an all-zero vector. Errors are shown in the panel. No gesture subscriptions or six-contact fake publishers remain.

ROS 2 publish calls include explicit `Float64MultiArray` and `GripperCommand` datatype definitions for Foxglove Bridge. Keep client publishing enabled for command panels, and connect only to the intended real or simulation bridge.

## Build and verify

```bash
npm ci
npm run typecheck
npm test
npm run build
npm run package
```

`npm test` uses Node's built-in test runner and compiles only the pure adapters. It checks shuffled complete keys, typed arrays, unknown/nonfinite/duplicate/missing slots, mapping reset and backwards time, heartbeat expiry, exact simulation vector shape, position-only joint feedback, and 3D palette stops/interpolation/clamping/unknown gray. It does not claim a live Foxglove/ROS integration test.

The package command produces a `.foxe` file in this directory. Import it into Foxglove's extension manager, then add **PNC Tactile Diagnostics** alongside the retained command panels and a 3D panel subscribed to the ROS surface overlay. No extension installation is performed by the build scripts.
