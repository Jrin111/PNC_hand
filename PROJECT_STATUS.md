# PNC Hand Project Status

Last updated: 2026-09-03

This is the living project context for engineers and Codex sessions taking over the repository.
Update the progress lists and change log whenever source, artifacts, target image, hardware
mapping, or validation status changes.

## Background and objective

The objective is one ROS 2 system on the Renesas RZ/V2H RDK that retains the proven Inspire
RH56E2 serial motion-control path while adding distributed NanoSen PNC tactile sensing. Nine
RAA2S4704 devices share one SPI bus and use nine independent GPIO chip-select lines. Up to 54
electrical channels are acquired directly by the RZ/V2H; the ESP32 is not in this tactile path.

The intended result is simultaneous hand actuation and tactile publication, followed by a
verified mapping from the 54 electrical slots to the final 47 physical tactile zones.

## Target and compatibility decision

The project target is ROS 2 Jazzy because the RZ/V2H RDK image used for this work provides Jazzy.
The checked-in AArch64 plugins/executable were also produced against the RZ/V2H Jazzy userspace.
This is a runtime compatibility requirement, not a documentation preference: `ros2_control`
topic/parameter behavior and C++ plugin ABI differ across ROS distributions.

- Source `/opt/ros/jazzy/setup.bash` on the RDK.
- Do not mix the included binaries with Humble or another ROS distribution.
- Rebuild the full affected dependency closure with the RZ/V2H Jazzy sysroot after any
  ABI-dependent C++ or interface change.

## Architecture and design decisions

- Keep actuator control and tactile acquisition in separate ROS 2 control hardware packages.
  Motion uses the root `/controller_manager`; tactile uses `/tactile/controller_manager`.
- Preserve the existing Inspire hardware interface and its position, velocity, force,
  force-threshold, and motion-mode behavior. Restore its original bringup, description, meshes,
  and gripper adapter rather than replacing the motion stack with a tactile-only launch.
- Acquire one complete tactile frame in each control-cycle `read()`. Map six physical channels per
  RAA device to stable logical names and export `raw_i`, `raw_q`, and filtered `value` interfaces.
- Hold every GPIO chip-select HIGH except during its selected SPI transaction. Check the response
  echo and CRC, isolate repeated per-device failures, and attempt periodic recovery while healthy
  devices continue to update.
- Publish the Jazzy `robot_description` topic from a namespaced
  `robot_state_publisher`, and pass broadcaster parameters through the spawner explicitly.
- Keep existing ROS package names because the checked-in install outputs and downstream launch
  references depend on them. `PNC_hand` remains the repository name.
- Version the target DTS, its patch, and matching DTB beside the ROS packages. The change fixes
  SDHI0 VccQ at 1.8 V, releases PA0 for GPIO, removes native SPI SS pins from the SPI pin group,
  and disables GPT use of P70/P71 so the required GPIO chip selects remain available.

## Components

| Path | Role |
| --- | --- |
| `src/robots/inspire_rh56e2_hand_ros2_control` | Inspire serial actuator hardware interface and `read()`/`write()` path |
| `src/robots/inspire_rh56e2_hand_bringup` | Position and trajectory launch, controller configuration, and tests |
| `src/robots/inspire_rh56e2_hand_description` | Complete left/right URDF, xacro, and mesh assets |
| `src/utils/dexhand_utils` | Standard gripper-action adapter and grasp-profile mappings |
| `src/robots/ssc_tactile_hand_ros2_control` | RZ/V2H SPI/GPIO tactile acquisition and processing |
| `src/robots/ssc_tactile_hand_bringup` | Tactile configuration, xacro, Jazzy launch, and configuration test |
| `src/utils/state_interfaces_broadcaster` | Pinned broadcaster used for tactile state publication |
| `install/` | Existing matching ROS 2 Jazzy/AArch64 package outputs |
| `platform/rzv2h/` | Modified DTS, reproducible patch, and matching WS125/RZ/V2H DTB |

## Completed repository work

- [x] Implemented direct nine-device RAA2S4704 acquisition with ordered GPIO chip selects.
- [x] Added response checking, CS safety, per-device isolation, and periodic recovery.
- [x] Added 54 logical sensors and 162 explicit tactile state interfaces.
- [x] Restored the complete Inspire motion-control source set: hardware interface, bringup,
      left/right description and meshes, and `dexhand_utils`.
- [x] Restored the matching existing Jazzy/AArch64 install outputs for motion and tactile packages.
- [x] Restored both Inspire position-control and trajectory-control launch paths.
- [x] Updated the restored Inspire Jazzy launch paths to pass each controller parameter file
      through its spawner while keeping `robot_description` on the root topic.
- [x] Fixed the confirmed Jazzy tactile startup blockers: a namespaced
      `robot_state_publisher` now supplies `robot_description`, and the broadcaster spawner receives
      its parameter file explicitly.
- [x] Corrected target setup documentation from Humble to Jazzy.
- [x] Added the modified RZ/V2H DTS, reproducible patch, and compiled DTB.
- [x] Completed available file-level consistency, static configuration, protocol, and artifact
      architecture checks.

No new cross-compilation was performed for the 2026-09-03 repository completion. The restored
motion artifacts are the existing outputs, and the accepted tactile startup changes affect only
launch/configuration metadata rather than compiled C++.

## Required target acceptance

- [ ] Deploy the current ROS install outputs and DTB to the intended RZ/V2H RDK image.
- [ ] Record the exact RDK/BSP image version and confirm `/opt/ros/jazzy`.
- [ ] Confirm `/dev/spidev1.0`, `/dev/gpiochip1`, GPIO ownership, and native SS electrical safety
      after booting the included DTB.
- [ ] Validate response echo and CRC behavior on every populated RAA device.
- [ ] Measure normal full-hand acquisition over at least 100 cycles and record average/max cycle
      time against the 40 Hz / 25 ms target.
- [ ] Press each finger and palm region and confirm the physical RAA/channel order.
- [ ] Finalize the mapping from 54 electrical slots to 47 physical tactile zones and add Newton
      calibration only after hardware data is available.
- [ ] Run combined Inspire position/trajectory actuation and tactile acquisition tests on the
      assembled hand.

## Review decisions and deferred changes

An external source review identified six items. Three are accepted and addressed in this update:

1. Jazzy requires `robot_description` on a topic for controller manager startup.
2. Jazzy controller parameters must be passed to the broadcaster spawner with `--param-file`.
3. Deployment instructions must use Jazzy rather than Humble.

The following code-level concerns are credible but intentionally not changed yet:

- Stale tactile values currently lack an agreed validity contract. Decide between NaN values,
  explicit online/age interfaces, or a hardware error policy before changing the interface.
- Recovery with `auto_tare=true` can re-tare under load. Decide whether recovery preserves the
  previous baseline or requires an explicit unloaded re-tare.
- Recovery selection can be unfair when `recovery_interval_frames=1`; the production default is
  40, but a rotating cursor is appropriate if interval 1 must be supported.

These changes alter runtime semantics and require a new matching AArch64 build. Changing only the
source while continuing to ship the old plugin would make the repository internally inconsistent.

## Known timing trade-off

Recovery currently runs synchronously inside tactile `read()`. A recovery attempt performs fixed
initialization delays and may exceed the nominal 25 ms cycle budget, even though healthy
acquisition resumes afterward. Normal acquisition at 40 Hz has not yet been measured on the
target. If recovery-time timing must also remain below 25 ms, design an incremental or asynchronous
recovery state machine and rebuild the plugin.

## Handoff to another computer or Codex session

1. Clone the private repository and read `README.md` and this file first.
2. Use the RZ/V2H ROS 2 Jazzy environment exactly; do not execute the AArch64 target binaries on
   the development host.
3. Treat `src/` as source of truth and `install/` as the currently paired target output. Do not
   rename ROS packages without rebuilding every dependent artifact and updating all references.
4. Do not rebuild merely for a handoff. When a compiled source change is accepted, use the
   Renesas Jazzy xbuild workflow and replace the complete affected dependency set.
5. Before deployment, record the Git commit, target image/BSP version, and SHA-256 hashes of the
   DTB and shared libraries.
6. After each hardware test, append the exact command, configuration, measured result, and any
   mapping correction to this file.

## Change log

| Date | Change |
| --- | --- |
| 2026-09-03 | Created the private GitHub project with the tactile source packages and matching AArch64 outputs. |
| 2026-09-03 | Added the modified WS125/RZ/V2H DTS, patch, compiled DTB, and living handoff document. |
| 2026-09-03 | Restored Inspire motion bringup, complete hand description/meshes, `dexhand_utils`, and their existing Jazzy/AArch64 install outputs. |
| 2026-09-03 | Fixed the accepted Jazzy motion/tactile launch issues and documented deferred runtime-policy changes. |
