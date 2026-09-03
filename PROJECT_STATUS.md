# PNC Hand Project Status

Last updated: 2026-09-03

This is the living project context for engineers and coding assistants taking over the repository.
Update the progress lists and change log whenever source, artifacts, target image, hardware
mapping, or validation status changes.

[CLAUDE.md](CLAUDE.md) records the user's requested main/subagent responsibilities, independent
review approach, focused test policy and state-maintenance workflow. Review-only instructions
apply to review-only tasks; existing user authorization governs later implementation work.

For an independent source/design review, read [the Chinese review guide](docs/REVIEW_GUIDE.zh-CN.md)
and inspect [PR #1](https://github.com/Jrin111/PNC_hand/pull/1). The implementation baseline is
`3ecf1d6891a4dd8057e88c578c4c514a67e420c0`; the Foxglove implementation is
`e5c43263a4517076c60ad469f3b99fc8256eb4e2` on `codex/pnc-foxglove-offline`.
Check the current branch and PR state before reviewing. The guide invites checking and correcting
these recorded conclusions; documentation updates are not new runtime or hardware validation.

The subsequent user-authorized review fixes change runtime behavior: failed-chip NaN output
in the tactile C++ plugin, tactile-only smoke injection, and strict frame lengths in extension
1.1.2. The original-package follow-up `08e031a` also repairs binary serial configuration and
profile width updates, and removes an ineffective timeout setting. All nine packages have been
built using the official RZ/V2H Jazzy sysroot in a separate workspace. Deploy that new overlay with matching target
dependencies; the preserved repository `install/` outputs do not include these fixes.

## Background and objective

The objective is one ROS 2 system on the Renesas RZ/V2H RDK that retains the proven Inspire
RH56E2 serial motion-control path while adding distributed NanoSen PNC tactile sensing. Nine
RAA2S4704 devices share one SPI bus and use nine independent GPIO chip-select lines. Up to 54
electrical channels are acquired directly by the RZ/V2H; the ESP32 is not in this tactile path.

The intended result is simultaneous hand actuation and tactile publication, followed by a
verified mapping from the 54 electrical slots to the final 47 physical tactile zones.

The user has now included Foxglove in the hardware-free phase: use the **left hand**, display
continuous tactile intensity directly on its 3D surface, retain the hand-control panels, and add
an independent simulation mode. Touch-gesture detection/automatic touch-triggered actions are
explicitly deferred. The camera keypoint package was reviewed but is not needed for this scope.

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
- In each control-cycle `read()`, sequentially scan active devices and publish a 54-slot snapshot.
  Healthy sampled devices update; unsampled/failed devices export NaN for that frame. Map six
  physical channels per RAA device to stable logical names and export `raw_i`, `raw_q`, and
  filtered `value` interfaces. A published snapshot is not proof of 54 new simultaneous samples.
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
| `src/utils/pnc_tactile_visualizer` | 47 frame-attached surface polygons, per-zone colors, strict named-channel decoding |
| `src/utils/pnc_hand_demo` | Left-hand mock motion and 54 synthetic channels in isolated ROS domain 77 |
| `foxglove/` | Updated panel extension, packaged `.foxe`, 3D layout and usage instructions |

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

No new cross-compilation was performed for the initial 2026-09-03 repository completion. The restored
motion artifacts are the existing outputs, and the accepted tactile startup changes affect only
launch/configuration metadata rather than compiled C++.

The later Foxglove work adds Python ROS packages and an optional bridge switch to the source
motion launch, and changes mock joint feedback to position-only (unmeasured force/velocity must
remain unknown). These additions are not present in the old `install/` tree. Development builds
use a separate install prefix; do not overwrite the target artifacts with a desktop build.

### Minimum target installation for the current source

| Package/component | Required action |
| --- | --- |
| `ssc_tactile_hand_ros2_control` | Rebuild/deploy the changed C++ plugin with matching RZ/V2H Jazzy xbuild and libgpiod 1.6; old binaries retain the fault freeze |
| `inspire_rh56e2_hand_ros2_control` | Deploy the new binary-safe serial plugin and updated controller config from `08e031a` |
| `dexhand_utils` | Deploy the adapter from `08e031a` so profile changes update the displayed width limit |
| `pnc_tactile_visualizer` | Install the new Python ROS package, mapping files and target Jazzy dependencies |
| `inspire_rh56e2_hand_bringup` | Reinstall launch/config resources for `launch_foxglove` and position-only mock feedback |
| `pnc_hand_demo` | Optional Python simulation package; not needed by the real acquisition/display path |
| `foxglove_bridge` | Confirm a compatible Jazzy target package is present; supply one if missing |
| Foxglove panel extension | Install the packaged 1.1.2 `.foxe` on the display computer; no xbuild step |

No native driver/broadcaster source changed in the initial Foxglove implementation commit.
The later review fix changes tactile driver C++, so rebuild that plugin and its affected
dependency set. The 162 state interface names/types are unchanged. The follow-up also changes
Inspire serial handling and the gripper adapter; use their new binaries. The unchanged
broadcaster can be reused when compatible. Python entry points, package index/resources
and dependencies still need correct installation. A separate RZ/V2H xbuild overlay has now been
produced; see the build record below. The old repository `install/` remains a historical artifact
set, not the output of the repaired source.

### Foxglove software implementation

- [x] Reused the supplied GitLab extension at `a3976ebb68718e7184eb16c3a5f255735f866130`,
      preserving its license and the Console/Force/Gripper panels.
- [x] Replaced six binary-contact pads with 54 named PNC channels and an explicitly enabled,
      heartbeat-gated simulation input. No touch-gesture node is launched.
- [x] Added left/right 47-zone CAD surface templates: 22 palm zones and five zones per fingertip.
      Templates follow the URDF TF links and support independent simultaneous colors.
- [x] Added unknown/NaN and stream-timeout handling; values are relative response, never Newtons.
- [x] Added a left-hand demo launch using mock motion, one bridge, and production-compatible
      162-key tactile messages, with fault/recovery inputs and independent ROS domain.
- [x] Added pure decoding/geometry/model tests, extension regression tests and an opt-in ROS
      integration check for colors, faults, mock joint feedback and TF attachment.

The original NanoSen manual and finger Gerber establish 25 finger + 22 palm regions. The existing
Inspire CAD does not include the added PNC carrier; mounting coordinates, board rotation and final
electrical mapping remain unverified. Real `channel` fields are null and `mapping_verified=false`;
only the explicit demo profile uses example wiring. A verified real profile requires all 47 actual
channel assignments. The repaired plugin now marks acquisition failures NaN; only deploying
the rebuilt plugin enables this on real hardware. Simulation does not certify SPI recovery.

The visualization is integrated with the existing acquisition interface at source level. Real
SPI `read()` updates 54 channels with `raw_i`, `raw_q`, and `value` (162 state interfaces);
the broadcaster publishes `/tactile/tactile_hand_state_broadcaster/names`
(`control_msgs/msg/Keys`) and `/tactile/tactile_hand_state_broadcaster/values`
(`control_msgs/msg/Float64Values`). The visualizer consumes those topics and publishes
`/tactile/markers` for Foxglove through the bridge. The diagnostic panel also subscribes directly
to the real names/values topics. Neither real display path requires the simulator.

All 54 electrical slots retain independent values, and all 47 mapped surface regions can show
different colors in the same frame. The manual simulation selection only chooses which stored
channel value to edit. Real acquisition scans the nine chips sequentially and commits the frame;
this supports multiple ongoing contacts but does not provide simultaneous sampling instants.
The configured 40 Hz / 25 ms acquisition period is still an unmeasured target on the RDK.

Real deployment is not yet a single-launch, hardware-validated setup. Existing real bringup does
not start `pnc_tactile_visualizer`; deploy/build the added package, start it with a verified mapping
and measured color range, retain the left-hand URDF/TF motion stack, and run one bridge in the
same ROS domain. The position-control launch starts a bridge by default; disable that bridge if
providing a separately configured one, and allow the uppercase `.STL` URDF assets as in the demo.
The old `install/` does not contain these additions. Diagnostic numeric values support real data,
but the panel's current color scale is fixed at 0..1 and may saturate for unnormalized hardware
responses; 3D colors have configurable range/gain/offset. Mapping, response range and physical
acquisition acceptance remain open even though ROS/mock and Foxglove GUI integration passed.

Historical validation of the initial implementation (extension 1.1.1):

- Five visualization core/geometry tests and eight simulator-model tests pass without ROS.
- Nine extension tests, TypeScript checking and production packaging pass; the installed local
  extension is version 1.1.1 and its JavaScript matches the tested production build exactly.
- The 22 automated tests are the 5 + 8 + 9 checks above. TypeScript checking, the live ROS
  acceptance script and actual GUI interaction are separate forms of validation. These results
  were recorded during the implementation; they are not a rerun of the later review fixes,
  and the manual observations are not checked-in hardware logs.
- A local Linux/AArch64 ROS 2 Jazzy container built the six-package demo dependency closure;
  no real SPI tactile package or RZ/V2H target artifact was used or overwritten.
- The live ROS integration check passed, including 162 exact keys, 47 valid TF
  patches, simultaneous independent colors, per-chip NaN fault/recovery, six mock joint position
  responses and patch movement with unchanged local geometry. This verifies ROS/mock behavior;
  it does not certify physical motors, ADC acquisition or force calibration.
- Foxglove Desktop GUI acceptance completed: the imported layout connects over WebSocket,
  displays the full URDF hand and colored patches, and receives 54/54 tactile channels.
  GUI clear and simultaneous ch0=0.30/ch1=0.80 inputs were observed in ROS and produced
  distinct palm colors. GUI FIST reached the commanded six mock positions with TF changes;
  OPEN restored all six positions to zero. These are simulator results, not physical feedback.
- A live pause of only the simulated tactile source grayed all 47 zones after about 0.66 seconds;
  resuming the source restored fresh heartbeat, finite values and colors. The controller manager
  and bridge continued running. Version 1.1.1 also fixes an observed short-height Console
  overlap by retaining readable card heights and scrolling; the corrected GUI was inspected.
- Added a Docker/Compose demo recipe using the same successfully installed dependencies;
  `docker compose config` passes. The recipe itself has not been rebuilt as a second image.
- The actual Foxglove client successfully connects at `ws://127.0.0.1:8765`, renders the model
  and markers, and publishes commands through the bridge. This supersedes the earlier
  inconclusive standalone Node WebSocket probe. Refresh the view after extension updates.

### Validation of the review repairs

- Built the complete `ssc_tactile_hand_ros2_control` package, including the real SPI shared
  library, in the existing Linux/AArch64 ROS 2 Jazzy container (hardware_interface 4.45.2,
  libgpiod 1.6.3). CTest passed 2/2 targets: existing protocol processing and new hardware
  validity tests. The latter executes the production hardware interface with only transport
  replaced by a fake bus: initial/inactive states, first chip failure, healthy-chip progress,
  isolation, unsuccessful/successful recovery, filter/tare preservation, disabled devices,
  closed transport, and all three chip-select fatal paths. No physical devices are opened.
- Extension 1.1.2: 12/12 tests, TypeScript checking, build and packaging passed. Short and long
  arrays/Float64Array frames and changed-name layouts are covered; the packaged bundle matches
  the production build. Version 1.1.2 is installed on the local display computer; installed JS
  matches the packaged JS byte-for-byte. Refresh Foxglove to load it. The `.foxe` is in
  `foxglove/foxglove_inspire_hand_panels/`.
- Simulator package: 15/15 offline Python tests (8 model + 7 smoke-isolation tests) passed.
  The smoke script runs against fake rclpy callbacks; tests verify its only publisher and
  normal/exception cleanup target `/pnc_demo/tactile_values`, rejection of real URDF even with
  `python -O`, duplicate descriptions, absent heartbeat and incorrect domain. No ROS commands
  were sent by these tests. The visualizer's 5 core tests also passed for NaN/gray behavior.
- Build/test files are separate in the review container: `/tmp/pnc-validity-build`,
  `/tmp/pnc-validity-install`, `/tmp/pnc-validity-log`, `/tmp/pnc-validity-test-log`.
  To reproduce in a Jazzy development workspace with dependencies installed:

  ```bash
  colcon build --base-paths src --packages-select ssc_tactile_hand_ros2_control \
    --build-base build --install-base install_local --cmake-args -DBUILD_TESTING=ON
  colcon test --build-base build --packages-select ssc_tactile_hand_ros2_control
  colcon test-result --test-result-base build --verbose
  ```

The checks above used a Linux/Jazzy native build and a fake bus. The subsequent official
RZ/V2H sysroot build is recorded below. Neither run validates physical SPI, new GUI motion
acceptance or a rebuilt Compose image. Board deployment, physical mapping, calibration and
acceptance remain pending.

### RZ/V2H sysroot cross-build

- Source: all nine `src/` packages from repair commit `0efb254`; no additional runtime source
  changes were needed to build. The final `cross-colcon-build` exited 0 with **9 packages
  finished**. The broadcaster reports an upstream `tl_expected` deprecation warning.
- Environment repository: user-specified `ubuntu_x_compilation` at `ed6520c`; companion
  `ubuntu_xbuild_toolchains` at `1a020f538e0b0cb63cba9e154d226a4358b84932` (`v1.3.0`).
  Loaded its cross-build skill and used `sysroot-rosdep-install`, `arm64-chroot`,
  `sysroot-fix` and `cross-colcon-build`.
- Image: `ghcr.io/renesas-rdk/rzv2h_ubuntu_xbuild:multiarch`, index digest
  `sha256:a8ffd965afd149f1054d17ac2bf76e261a84c4475b9888b9372c9900480ade63`.
  Container `pnc-rzv2h-xbuild` runs on ARM64; compilation still uses the separate
  `/opt/arm64_sysroot`, GCC 13.3.0 `aarch64-linux-gnu-g++`, and `-mcpu=cortex-a55`.
- Target dependencies include hardware_interface/controller_interface **4.45.2**, libgpiod
  **1.6.3** (`libgpiod2t64`, SONAME `libgpiod.so.2`), Foxglove Bridge **3.4.1**, glibc
  **2.39-0ubuntu8.7**, and libstdc++ **14.2.0-4ubuntu2~24.04.1**. The SONAME does not mean the
  libgpiod 2.x API. The eventual board image and runtime versions still need reconciliation.
- Environment corrections: refreshed stale target apt indexes after a runtime-dependency
  download returned 404; installed the code generator's declared `python3-jinja2` and
  `python3-typeguard` dependencies on the container host. Restored the full sysroot source
  manifests before resolving `exec_depend`, which the v1.3.0 build-dependency helper removes.
  No toolchain, CPU target or project dependency metadata was changed.
- Validation: copied the install tree to `/opt/pnc_review/install` inside the target sysroot.
  Both cross-built tactile test executables passed there (protocol/processing plus all five
  hardware-validity groups). The three production shared libraries loaded successfully;
  all nine relocated ROS package indexes, both Python node modules, ROS imports, entry points,
  heatmap files and bringup resources passed their installation checks. No nodes, hardware
  devices or motor commands were started by this check.
- Local build root: `/Users/jr/Documents/ChatGPT/Multiple tactile sensor/xbuild_review/ros2_ws`;
  outputs are its standard `build/`, `install/`, `log/`. The new overlay is **local only** and
  does not overwrite or ship through the Git-tracked historical `PNC_hand/install/`.
  Reproduction commands are in [README.md](README.md#full-build-after-a-native-source-or-abi-change).
- Evidence under that build root: `log/pnc-cross-build-retry.log`,
  `log/pnc-target-validity-tests.log`, `log/pnc-target-load-check.log`,
  `log/pnc_sysroot_check.py`, and `log/pnc-target-versions.txt`.
  The new tactile plugin SHA-256 is
  `e85139019c423835db59020477e6f1b8c487464c83e6bed72f004f8dd56312a2`.

This establishes build and load compatibility with the recorded sysroot, plus the targeted
fake-bus behavior. It does not establish compatibility with an unrecorded board image or
complete the SPI/DTB/wiring/40 Hz hardware acceptance below.

### Original-package follow-up using the same xbuild environment

- Installed `arm64-cross-build` and `arm64-ros2-package-conventions` into
  `/Users/jr/.codex/skills/` from toolchain commit `1a020f5`, using the skill installer.
  Both installed trees match the cloned source, including their referenced files.
- Independent source checks covered the original serial/SPI plugins, broadcaster, gripper
  adapter, real launch/controller configuration, 162-channel contract, QoS, TF names and
  installed resources. The real tactile/display interface remains consistent.
- `08e031a` fixes two original runtime defects: inherited terminal transformations could
  corrupt binary hand responses, and profile changes did not update `/gripper_max_width`.
  Serial configuration now uses raw bytes and cleans up failed opens; profile changes publish
  the new latched width outside the profile lock. No motion protocol or tactile mapping changed.
- Removed `command_timeout: 0.5` from both copies of the position controller YAML: Jazzy's
  position controller does not implement it. While active it retains its last target; stopping
  publication is not an automatic stop mechanism. This correction changes no runtime policy.
- All nine packages cross-built successfully again (`log/pnc-cross-build-native-repairs.log`).
  Final package refreshes are in `log/pnc-serial-final-build.log` (explicit test constructor,
  no warning) and `log/pnc-adapter-final-build.log` (declared Python test dependencies).
- Before/after target-sysroot regressions: the old serial plugin fails activation under inherited
  byte-stripping flags; the new plugin passes real lifecycle/read calls through a PTY, preserves
  CR/LF/high-bit/XON/XOFF feedback bytes, and closes descriptors after successful/failed activation.
  Logs: `log/pnc-serial-before.log` and `log/pnc-serial-after.log`.
  The wrapper does not mount `/dev/pts`; the local `log/pnc_run_serial_test.sh` temporarily mounts
  a private devpts instance inside `arm64-chroot` and removes it afterward. No toolchain edit.
- The old adapter switches internally to pinch but never sends the 0.100 m limit. The new one
  passes 0.120 → 0.100 → 0.120 updates, late-subscriber delivery and an isolated no-command
  observation. Logs: `log/pnc-profile-width-before.log` and `log/pnc-profile-width-after.log`.
  The latter also repeats the nine-package relocation/import/three-plugin load check with
  the new binaries. Both regressions use the installed target programs and no physical hardware.
- Independent final diff review found no remaining blocking regression in these repairs.
  The gripper action's elapsed-time feedback/result still does not prove physical arrival;
  the regular gripper topic remains open loop. Real position feedback must be used on the board.
- Local deployment archive:
  `/Users/jr/Documents/ChatGPT/Multiple tactile sensor/xbuild_review/pnc-hand-rzv2h-jazzy-08e031a.tar.gz`
  (17,638,288 bytes), SHA-256
  `89c3c48746bda9c78b8e03b0b66b0db92ad59d71c493678c74f7863b73b6b32b`.
  Contains the complete nine-package `install/`, `BUILD_INFO.json`, `SHA256SUMS`, `DEPLOY.txt`
  and selected validation logs. All 379 installed regular files were verified against the
  archive; all four ELF outputs are AArch64. Its staged source matches `08e031a` byte-for-byte.
  This archive is local only; GitHub contains the source and reproducible build instructions.
  It does not bundle system/ROS runtime packages or the DTB.

## Required target acceptance

- [ ] Deploy the current ROS install outputs and DTB to the intended RZ/V2H RDK image.
- [ ] Record the exact RDK/BSP image version and confirm `/opt/ros/jazzy`.
- [ ] Confirm `/dev/spidev1.0`, `/dev/gpiochip1`, GPIO ownership, and native SS electrical safety
      after booting the included DTB.
- [ ] Reconcile the earlier "SPI6" project label, the supplied DTS `&spi0` node and Linux
      `/dev/spidev1.0` against the actual SoC/BSP and connector wiring. Confirm the WS125-specific
      fixed-1.8-V SDHI0 assumption applies to the target board.
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

The later Claude review led to three accepted repairs:

- Failed, isolated, disabled or not-yet-sampled chips export NaN in all three interfaces of all
  six channels, starting at the first failed acquisition. Successful complete reads restore
  measurements; global errors/deactivation invalidate every exported state. Integer EMA/tare
  history remains separate. This replaces the earlier failed-chip last-value hold policy.
- The smoke script only publishes to `/pnc_demo/tactile_values`, including cleanup; it no longer
  creates a motor command publisher or tests active joint motion. Mock URDF validation is an
  explicit runtime check even under `python -O`. Domain/heartbeat checks remain, with a rejection
  of already-discovered duplicate descriptions as an additional check, not proof of isolation.
- Extension 1.1.2 requires `values.length` to match the full received `keys.length`; malformed
  lengths make every measurement unknown. This does not identify same-length cross-generation
  names/values reorderings because the existing message contract has no shared generation ID.

The following concerns remain outside these repairs:

- An acquired I sample of zero retains that channel's earlier filtered I value before subtracting
  tare, even when its raw I/Q fields update. Review whether this firmware-compatible policy is
  appropriate for physical release/invalid-sample behavior; it is separate from a failed-chip
  freeze and is not covered by simply detecting whole-message timeout.
- Recovery with `auto_tare=true` can re-tare under load. Decide whether recovery preserves the
  previous baseline or requires an explicit unloaded re-tare.
- Recovery selection can be unfair when `recovery_interval_frames=1`; the production default is
  40, but a rotating cursor is appropriate if interval 1 must be supported.

The accepted NaN fix already requires a matching AArch64 plugin rebuild. Any later native
recovery changes also require rebuilding; do not deploy old outputs as if they match new source.

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
3. Treat `src/` as source of truth and the Git-tracked `install/` as historical target output.
   Use the new isolated xbuild overlay for the repaired source, after checking the board ABI. Do not
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
| 2026-09-03 | Added the requested left-hand 3D tactile heatmap, updated Foxglove extension, and independent hardware-free simulation; deferred touch gestures per user direction. |
| 2026-09-03 | Completed actual Foxglove GUI input/rendering and mock motion validation; installed extension 1.1.1 to fix overlapping Console controls in short panels. |
| 2026-09-03 | Documented the real SPI-to-Foxglove source integration, independent multi-contact behavior, 54 electrical slots versus 47 physical zones, and the remaining mapping/range/build/startup requirements; no real-hardware validation is claimed. |
| 2026-09-03 | Added an independent Claude/engineer review guide and clarified bringup changes, minimum target installation, evidence boundaries, zero-I retention, and board/SPI naming checks. Documentation only; no runtime behavior, target artifacts or validation status changed. |
| 2026-09-03 | Added CLAUDE.md with the user's main/Explore/Test/Review agent roles, independent architecture review requirements, original-package reading order, focused testing and project-state handoff rules; aligned the review guide with existing user authorization. Documentation only. |
| 2026-09-03 | Addressed review findings: failed/unmeasured chip states become NaN in the real driver; smoke publishes only simulated tactile input; extension 1.1.2 rejects mismatched frame lengths. Target plugin rebuild/deployment and hardware acceptance remain pending. |
| 2026-09-03 | Cloned the user-specified xbuild environment and loaded its companion skill. Cross-built all nine packages at `0efb254` with the official RZ/V2H Jazzy sysroot; target-sysroot fake-bus and relocated-install checks passed. New install remains in the separate local xbuild workspace; board deployment and hardware acceptance remain pending. |
| 2026-09-03 | Installed cross-build/package skills; reviewed the original packages and fixed serial byte preservation/descriptor cleanup plus profile-width publication in `08e031a`. Corrected unsupported timeout configuration, rebuilt the nine-package overlay and passed targeted old/new sysroot regressions. |
