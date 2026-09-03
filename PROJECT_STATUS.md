# PNC Hand Project Status

Last updated: 2026-09-03

This is the living project context for engineers and Codex sessions taking over the repository.
Update the progress checklist and change log whenever the source, target image, hardware mapping,
or validation state changes.

## Background

The project adds distributed NanoSen PNC tactile sensing to an Inspire RH56E2 dexterous hand.
Nine RAA2S4704 devices are connected directly to a Renesas RZ/V2H board through one SPI bus and
nine GPIO chip-select lines. The target ROS 2 stack must operate the existing Inspire hand while
publishing tactile state from up to 54 electrical channels.

## Design

- Keep actuator control and tactile acquisition in separate ROS 2 control hardware packages.
  This limits changes to the existing Inspire serial protocol and lets each path be tested
  independently.
- Use `ssc_tactile_hand_bringup` to own the target configuration and launch the tactile hardware
  together with `state_interfaces_broadcaster`.
- Acquire one complete tactile frame in each control-cycle `read()`. Map six physical channels
  per device to stable logical sensor names and publish raw I, raw Q, and filtered values.
- Hold every GPIO chip-select HIGH except during its selected SPI transaction. Validate the RAA
  response echo and CRC, isolate a failing device after repeated errors, and retry recovery
  periodically while healthy devices continue to update.
- Keep the original ROS package names. `PNC_hand` is the repository name; renaming packages would
  break the included install artifacts and downstream launch/configuration references.
- Version the target device-tree source and compiled DTB with the ROS packages. The WS125 change
  fixes SDHI0 VccQ at 1.8 V, releases PA0 for GPIO, removes native SPI SS pins from the SPI pin
  group, and disables GPT use of P70/P71 so the required GPIO chip selects remain available.

## Components

| Path | Role |
| --- | --- |
| `src/robots/inspire_rh56e2_hand_ros2_control` | Inspire actuator position, velocity, force, threshold, and motion-mode interfaces |
| `src/robots/ssc_tactile_hand_ros2_control` | RZ/V2H SPI/GPIO tactile acquisition and processing |
| `src/robots/ssc_tactile_hand_bringup` | Tactile configuration, URDF/xacro, and launch |
| `src/utils/state_interfaces_broadcaster` | Pinned broadcaster used for tactile state publication |
| `install/` | Matching prebuilt AArch64 ROS 2 package outputs |
| `platform/rzv2h/` | Modified DTS, patch, and matching WS125/RZ/V2H DTB |

## Current progress

- [x] Implemented the nine-device RAA2S4704 tactile hardware interface.
- [x] Added strict response checking, CS safety, per-device isolation, and periodic recovery.
- [x] Added tactile bringup and the required state-interface broadcaster.
- [x] Included the Inspire RH56E2 control package and its angle/velocity/force `read()` path.
- [x] Cross-compiled the revised tactile packages for AArch64 and retained the matching outputs.
- [x] Included the existing matching AArch64 Inspire control output.
- [x] Added the modified RZ/V2H DTS, reproducible patch, and compiled DTB.
- [x] Completed the available static/unit/build checks.
- [ ] Deploy the current ROS install outputs and DTB to the target board.
- [ ] Confirm the target `spidev` node and that the controller's native SS output is electrically
      safe while GPIO chip selects are used.
- [ ] Validate echo/CRC behavior on all populated RAA devices.
- [ ] Measure sustained full-hand acquisition at the selected 40 Hz controller rate.
- [ ] Confirm the mapping from 54 electrical channel slots to the final 47 physical tactile zones.
- [ ] Run combined Inspire actuation and tactile acquisition tests on the assembled hand.

## Known design trade-off

Device recovery currently runs synchronously inside the tactile `read()` path. A recovery attempt
can temporarily exceed the nominal 25 ms cycle budget because it reinitializes and re-tares a
device. Healthy acquisition continues afterward, but a later revision may move recovery to an
incremental or asynchronous state machine if target timing measurements require it.

## Handoff to another Codex session

1. Clone the private repository and read `README.md` and this file first.
2. Treat `src/` as the source of truth. Do not rename ROS packages without rebuilding every
   dependent artifact and updating all launch/configuration references.
3. Do not rebuild merely to prepare a handoff. Rebuild only after source, toolchain, sysroot, or
   target ABI changes.
4. Before deploying, verify the DTB and shared-library architecture and record their hashes.
5. After each meaningful change, update the checklist and append one short entry below containing
   the date, commit, verification performed, and any new artifact provenance.

## Change log

| Date | Change |
| --- | --- |
| 2026-09-03 | Created the private GitHub project structure with four ROS 2 source packages and matching AArch64 install outputs. |
| 2026-09-03 | Added the modified WS125/RZ/V2H DTS, its patch, the matching DTB, and this living handoff document. |
