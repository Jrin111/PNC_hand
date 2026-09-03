# PNC Hand

PNC Hand integrates the existing Inspire RH56E2 motion stack with direct NanoSen PNC tactile
acquisition on a Renesas RZ/V2H RDK. The repository contains the complete ROS 2 source set used
by the hand, matching prebuilt AArch64 install outputs, and the RZ/V2H device-tree files required
for the shared SPI and GPIO chip-select wiring.

See [PROJECT_STATUS.md](PROJECT_STATUS.md) for the living engineering context, design decisions,
verified progress, open review items, hardware acceptance work, and handoff notes.

## Required target: ROS 2 Jazzy

ROS 2 Jazzy is required. The Renesas RZ/V2H RDK image used by this project provides Jazzy, and the
included AArch64 plugins and executable were built against that Jazzy userspace. Do not source
Humble or combine these prebuilt artifacts with another ROS distribution: `ros2_control` plugin
APIs, launch behavior, and C++ ABI dependencies may not match.

If ABI-dependent source changes, rebuild the complete affected dependency set with the same
RZ/V2H Jazzy sysroot. The current repository update reuses the existing binaries; it does not
claim a new cross-build.

## System design

Motion and tactile acquisition intentionally use separate hardware packages and controller
managers:

- Motion path: Inspire serial protocol -> `inspire_rh56e2_hand_ros2_control` -> root
  `/controller_manager` -> position or trajectory controller, force-threshold controller,
  motion-mode controller, and the optional `dexhand_utils` gripper adapter.
- Tactile path: RZ/V2H SPI6 plus nine GPIO chip-select lines -> nine RAA2S4704 devices ->
  `ssc_tactile_hand_ros2_control` -> `/tactile/controller_manager` -> 162 `raw_i`, `raw_q`, and
  filtered `value` state interfaces -> `state_interfaces_broadcaster`.

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
install/                                 # matching existing Jazzy/AArch64 outputs
platform/rzv2h/                          # DTS, reproducible patch, and compiled DTB
```

The ROS package names are retained for compatibility with the included artifacts and downstream
launch/configuration references. `PNC_hand` is the repository-level project name.

## Use the included target artifacts

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

## Build from source after a source or ABI change

Stage all packages under `src/` in the standard Renesas Ubuntu xbuild workspace, install sysroot
dependencies, then build both dependency closures against the RZ/V2H Jazzy sysroot:

```bash
sysroot-rosdep-install /home/ubuntu/ros2_ws
cd /home/ubuntu/ros2_ws
cross-colcon-build --packages-up-to \
  inspire_rh56e2_hand_bringup ssc_tactile_hand_bringup
```

Replace the checked-in `install/` outputs only after the target architecture, ROS distribution,
source revision, and relevant tests have been recorded.

## Current validation boundary

Repository consistency and configuration checks are available, but end-to-end completion still
requires the RZ/V2H board and assembled hand. In particular, verify device enumeration, SPI
echo/CRC behavior, physical channel order, normal-frame 40 Hz timing, and combined motion plus
tactile operation. See `PROJECT_STATUS.md` before deployment.
