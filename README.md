# PNC Hand

PNC Hand integrates the existing Inspire RH56E2 motion stack with direct NanoSen PNC tactile
acquisition on a Renesas RZ/V2H RDK. The intended behavior is to display the strength and location
of multiple ongoing contacts directly on the left-hand 3D model in Foxglove, while retaining the
existing hand controls. The repository contains the ROS 2 sources, earlier Jazzy/AArch64 install
outputs for the original packages, and board-specific SPI/GPIO device-tree files. The earlier
install outputs do not include the new visualization packages or updated motion launch.

See [PROJECT_STATUS.md](PROJECT_STATUS.md) for the living engineering context, design decisions,
verified progress, open review items, hardware acceptance work, and handoff notes.

For the new **left-hand 47-zone Foxglove heatmap and independent hardware-free demo**, start
with [foxglove/README.md](foxglove/README.md). The demo uses the existing mock motion path and
54 synthetic tactile channels. Touch-gesture recognition and camera overlays are outside this
iteration. The earlier `install/` artifacts do not contain these new packages or launch changes;
build into a separate `install_local/` for development.

## Independent review / 独立评审入口

Claude should follow [CLAUDE.md](CLAUDE.md) for the user-requested main/subagent roles,
independent evaluation of the original architecture and changes, focused validation, and status upkeep.

For Claude or another engineer reviewing the implementation and design, begin with
[中文评审指南](docs/REVIEW_GUIDE.zh-CN.md), then inspect the linked source files and
[PR #1](https://github.com/Jrin111/PNC_hand/pull/1). The guide records the user's requirements,
implementation scope, build/deployment boundaries, reported evidence, and open review questions.
Its statements are claims to check against the code, not a substitute for that review.

The Foxglove implementation commit is `e5c43263a4517076c60ad469f3b99fc8256eb4e2`, based on
`3ecf1d6891a4dd8057e88c578c4c514a67e420c0`. Review branch `codex/pnc-foxglove-offline` and
verify the current PR status; do not assume an unmerged feature is already present on `main`.
The next two commits clarified that implementation. The later review fixes add failed-chip
NaN output in the C++ tactile driver, remove motor commands from the demo smoke check, and
ship extension 1.1.2 with mismatched-frame rejection. All nine ROS packages at repair commit
`0efb254` now build with the official RZ/V2H Jazzy xbuild sysroot; target-sysroot load checks and
the two tactile test executables pass. Board deployment and hardware acceptance remain pending.

## What works, and what remains to be established

| Requirement | Implementation and evidence | Remaining work |
| --- | --- | --- |
| RZ/V2H directly acquires nine RAA devices | Existing SPI/GPIO hardware plugin exports 54 channels, each with `raw_i`, `raw_q`, and filtered `value` | Target SPI/echo/CRC, wiring and timing acceptance |
| Multiple contacts with independent strengths | Driver tracks independent channel values; ROS/mock and actual Foxglove checks exercised two different strengths together | Verify physical multi-contact response; sequential scanning is not simultaneous sampling |
| Touch colors on the moving left hand | 47 link-attached surface templates; color and mock joint/TF movement checked | Confirm all physical channel assignments, mounting geometry and response range |
| Real data reaches Foxglove | Visualizer and diagnostic panel consume the original names/values interface; simulation uses that same interface | Install the new packages and launch configuration on the RDK; run the documented real startup sequence |
| Clear missing-data behavior | Failed or unmeasured chips now export NaN; display paths mark nonfinite values unknown, with a separate stream timeout | Deploy the repaired SPI plugin with matching target dependencies and verify real failures/recovery; the old binary still freezes values |

The 54 entries are electrical slots (9 devices × 6), while the 47 surface regions are 22 palm
and 25 finger regions. The remaining seven slots have no surface patch in this model. Real
mapping fields are unset. Displayed PNC values are relative response, not calibrated Newtons.
Touch-gesture recognition, automatic touch-triggered actions and camera overlays are deferred.

## Required target: ROS 2 Jazzy

ROS 2 Jazzy is required. The Renesas RZ/V2H RDK image used by this project provides Jazzy, and the
included AArch64 plugins and executable were built against that Jazzy userspace. Do not source
Humble or combine these prebuilt artifacts with another ROS distribution: `ros2_control` plugin
APIs, launch behavior, and C++ ABI dependencies may not match.

Rebuild changed native source and its affected dependency set with the same RZ/V2H Jazzy
sysroot. The review fix changes `ssc_tactile_hand_ros2_control`: its old binary must be replaced
to obtain failed-chip NaN output. Other compatible unchanged binaries can be reused. The
checked-in `install/` has not been replaced and is not a build of these fixes.

## System design

Motion and tactile acquisition intentionally use separate hardware packages and controller
managers:

- Motion path: Inspire serial protocol -> `inspire_rh56e2_hand_ros2_control` -> root
  `/controller_manager` -> position or trajectory controller, force-threshold controller,
  motion-mode controller, and the optional `dexhand_utils` gripper adapter.
- Tactile path: the RZ/V2H shared SPI bus plus nine GPIO chip-select lines -> nine RAA2S4704 devices ->
  `ssc_tactile_hand_ros2_control` -> `/tactile/controller_manager` -> 162 `raw_i`, `raw_q`, and
  filtered `value` state interfaces -> `state_interfaces_broadcaster`.
- Visualization path: the tactile broadcaster's `names`/`values` ->
  `pnc_tactile_visualizer` -> `/tactile/markers` -> one `foxglove_bridge` -> Foxglove 3D.
  The diagnostic panel consumes the same names/values directly. Existing hand URDF/TF supplies
  the moving model. Neither real display path requires `pnc_hand_demo`.
- Simulation path: an isolated domain 77 runs the original mock motion stack plus synthetic
  tactile messages, the visualizer, and one bridge. It validates the ROS/display interface, not
  the physical SPI transport, ADC behavior or real motor feedback.

This separation preserves the established RH56E2 motion behavior while allowing the direct
RZ/V2H tactile path to be configured, recovered, and validated independently. The ESP32 is not in
the tactile acquisition path.

## Repository layout

```text
src/
  robots/
    inspire_rh56e2_hand_ros2_control/   # serial hardware interface
    inspire_rh56e2_hand_bringup/        # position/trajectory launch and controllers
    inspire_rh56e2_hand_description/    # left/right URDF and meshes
    ssc_tactile_hand_ros2_control/      # SPI/GPIO tactile hardware interface
    ssc_tactile_hand_bringup/           # tactile launch, xacro, and configuration
  utils/
    dexhand_utils/                       # gripper-action adapter
    state_interfaces_broadcaster/        # pinned tactile state broadcaster
    pnc_tactile_visualizer/              # 47 surface patches, named values and colors
    pnc_hand_demo/                       # isolated hardware-free motion/tactile demo
foxglove/                               # extension source, .foxe, layout and quick start
install/                                 # matching existing Jazzy/AArch64 outputs
platform/rzv2h/                          # DTS, reproducible patch, and compiled DTB
```

The ROS package names are retained for compatibility with the included artifacts and downstream
launch/configuration references. `PNC_hand` is the repository-level project name.

## Use the included original target artifacts

This section starts the original target packages. For the complete new Foxglove stack, also
install the updates listed below and follow [real-data startup](foxglove/README.md#接入真实手).

Run the following in every target terminal from the repository root:

```bash
source /opt/ros/jazzy/setup.bash
source install/dexhand_utils/share/dexhand_utils/local_setup.bash
source install/inspire_rh56e2_hand_description/share/inspire_rh56e2_hand_description/local_setup.bash
source install/inspire_rh56e2_hand_ros2_control/share/inspire_rh56e2_hand_ros2_control/local_setup.bash
source install/inspire_rh56e2_hand_bringup/share/inspire_rh56e2_hand_bringup/local_setup.bash
source install/state_interfaces_broadcaster/share/state_interfaces_broadcaster/local_setup.bash
source install/ssc_tactile_hand_ros2_control/share/ssc_tactile_hand_ros2_control/local_setup.bash
source install/ssc_tactile_hand_bringup/share/ssc_tactile_hand_bringup/local_setup.bash
```

The files under `install/` are target artifacts for the RZ/V2H AArch64/Jazzy environment. Do not
try to run their ELF binaries on an x86 development computer.

## Launch motion and tactile acquisition

Use separate terminals with the same `ROS_DOMAIN_ID`. Start one motion mode only.

Position control:

```bash
ros2 launch inspire_rh56e2_hand_bringup \
  inspire_rh56e2_hand_joint_position_control.launch.py \
  serial_port:=/dev/ttyUSB0 hand_side:=left
```

Trajectory control alternative:

```bash
ros2 launch inspire_rh56e2_hand_bringup \
  inspire_rh56e2_hand_joint_trajectory_control.launch.py \
  serial_port:=/dev/ttyUSB0 hand_side:=left
```

Tactile acquisition:

```bash
ros2 launch ssc_tactile_hand_bringup ssc_tactile_hand.launch.py namespace:=tactile
```

The tactile broadcaster publishes:

- `/tactile/tactile_hand_state_broadcaster/names`
- `/tactile/tactile_hand_state_broadcaster/values`

The tactile defaults use `/dev/spidev1.0` and `/dev/gpiochip1`. Reconfirm those device names after
booting the included DTB before operating the hardware.

Hardware naming still needs a target-level check: earlier project notes call the bus "SPI6",
the supplied DTS uses `&spi0`, and Linux is configured here through `/dev/spidev1.0`. Those
names alone do not establish their correspondence. Match the SoC/BSP controller, connector pins,
and post-boot enumeration. The included DTB also assumes the WS125 SDHI0 supply is fixed at
1.8 V; confirm board/BSP compatibility before using it on the final hardware.

## Packages to install for the Foxglove update

The initial Foxglove implementation left native code unchanged. The subsequent fault-validity
fix changes the tactile driver C++; rebuild that plugin. Inspire motor control and the state
broadcaster remain unchanged and their compatible Jazzy/AArch64 binaries can be reused.

| Package/component | Required for real hardware display? | This update requires |
| --- | --- | --- |
| `ssc_tactile_hand_ros2_control` | Yes, for the repaired fault display | Cross-build and deploy the changed SPI plugin against the matching RZ/V2H Jazzy sysroot; include libgpiod 1.6 runtime |
| `pnc_tactile_visualizer` | Yes | Install the new `ament_python` package, its entry point and mapping files, with target Jazzy/Python dependencies |
| `inspire_rh56e2_hand_bringup` | Yes, for the documented shared-bridge startup | Reinstall the updated launch/config resources; this package has no native compile target |
| `pnc_hand_demo` | No; optional simulation | Install the new `ament_python` package only where the demo will run |
| `foxglove_bridge` | Yes | Reuse an installed compatible target version, or supply its Jazzy/AArch64 package if absent |
| `foxglove/foxglove_inspire_hand_panels` | On the display computer | TypeScript/npm packaging into `.foxe`; extension 1.1.2 is included and does not use xbuild |

The two bringup changes are an optional `launch_foxglove` switch (default `true`) and
position-only mock joint feedback. The real controller configuration is unchanged. Setting
`launch_foxglove:=false` allows one separately configured bridge to serve the whole system.
Real startup currently uses separate motion, tactile, visualizer and bridge commands; the
original motion launch does not automatically start the tactile visualizer.

The official RZ/V2H Jazzy xbuild has now produced all nine packages from `0efb254`, including
the repaired tactile plugin, Python entry points, ROS package index and resources. The new
overlay is in the separate local `xbuild_review/ros2_ws/install/` workspace; it has not replaced
the historical repository `install/` or been deployed to a board. See
[the build record](PROJECT_STATUS.md#rzv2h-sysroot-cross-build) for its location, dependency
versions and validation. Confirm these dependencies against the actual board image before use.

## Full build after a native source or ABI change

Use the user-specified [ubuntu_x_compilation](https://partnergitlab.renesas.solutions/pai/ros2/utility/ubuntu_x_compilation)
environment and the companion [arm64-cross-build skill](https://github.com/renesas-rdk/ubuntu_xbuild_toolchains/blob/1a020f538e0b0cb63cba9e154d226a4358b84932/.github/skills/arm64-cross-build/SKILL.md).
Stage the full `src/` tree in a separate workspace mounted at `/home/ubuntu/ros2_ws`.
The following commands run **inside the xbuild container**, with `PRODUCT=V2H` and
`ARM64_SYSROOT=/opt/arm64_sysroot`. This builds all packages, including the optional demo:

```bash
cd /home/ubuntu/ros2_ws

# Host Python runs the generate_parameter_library code generator.
sudo apt-get update
sudo apt-get install -y --no-install-recommends python3-jinja2 python3-typeguard

# Build and runtime dependencies belong in the separate target sysroot.
arm64-chroot apt-get update
sysroot-rosdep-install

# Toolchain v1.3.0 strips exec_depend from its copied package.xml files.
# Restore the full manifests before resolving runtime dependencies.
sudo rsync -a src/ "$ARM64_SYSROOT/home/ubuntu/ros2_ws/src/"
arm64-chroot bash -c 'export ROS_VERSION=2 ROS_PYTHON_VERSION=3 ROS_DISTRO=jazzy; rosdep install --from-paths /home/ubuntu/ros2_ws/src --ignore-src --dependency-types exec -y'
sysroot-fix
cross-colcon-build
```

The host Python packages above serve build-time code generation; they do not supply target
runtime libraries. Keep the wrapper and toolchain unchanged, and use the standard `build/`,
`install/`, `log/` directories in that isolated workspace. No `--symlink-install` was used.
On the matching board, source `/opt/ros/jazzy/setup.bash` and then the new overlay's
`install/local_setup.bash`. Install the target runtime dependencies too; the overlay does not
bundle ROS, Foxglove Bridge or system libraries.

## Current validation boundary

Repository consistency and configuration checks are available, but end-to-end completion still
requires the RZ/V2H board and assembled hand. In particular, verify device enumeration, SPI
echo/CRC behavior, physical channel order, normal-frame 40 Hz timing, and combined motion plus
tactile operation. See `PROJECT_STATUS.md` before deployment.
