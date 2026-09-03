# PNC Hand

ROS 2 packages for the Inspire RH56E2 hand with the RZ/V2H-direct NanoSen PNC tactile stack.

## Repository layout

```text
src/
  robots/
    inspire_rh56e2_hand_ros2_control/
    ssc_tactile_hand_ros2_control/
    ssc_tactile_hand_bringup/
  utils/
    state_interfaces_broadcaster/
install/
  inspire_rh56e2_hand_ros2_control/
  ssc_tactile_hand_ros2_control/
  ssc_tactile_hand_bringup/
  state_interfaces_broadcaster/
platform/
  rzv2h/
    dts/rzv2h-rdk-ver1.dts
    dtb/rzv2h-rdk-ver1-ws125-raa2s4704.dtb
    patches/rzv2h-rdk-ver1-ws125-raa2s4704.patch
```

The ROS package names are intentionally retained so that the source remains compatible with the
included AArch64 install artifacts. The project name `PNC_hand` is the repository-level name.

See [PROJECT_STATUS.md](PROJECT_STATUS.md) for the project background, design rationale, current
progress, open validation work, and the living handoff log.

## Included functionality

- Inspire RH56E2 position, velocity, force, force-threshold, and motion-mode interfaces.
- PNC tactile acquisition through the RZ/V2H SPI and GPIO interfaces.
- Per-device fault isolation and recovery for the tactile acquisition path.
- Bringup configuration and launch files for the tactile hand.
- A pinned `state_interfaces_broadcaster` dependency.
- The WS125/RZ/V2H device-tree source, its patch, and the matching compiled DTB.

## Prebuilt AArch64 artifacts

The `install/` directory contains the existing cross-compiled ROS 2 install outputs that match
the source packages in this repository. No rebuild was performed while preparing this repository.

On the target, source the required ROS 2 installation first, then source the individual package
outputs in dependency order:

```bash
source /opt/ros/humble/setup.bash
source install/state_interfaces_broadcaster/share/state_interfaces_broadcaster/local_setup.bash
source install/inspire_rh56e2_hand_ros2_control/share/inspire_rh56e2_hand_ros2_control/local_setup.bash
source install/ssc_tactile_hand_ros2_control/share/ssc_tactile_hand_ros2_control/local_setup.bash
source install/ssc_tactile_hand_bringup/share/ssc_tactile_hand_bringup/local_setup.bash
```

## Build from source

Place the directories under `src/` in the RZ/V2H cross-build workspace and use the standard
Renesas Ubuntu xbuild workflow. Build the bringup package with its dependencies:

```bash
cross-colcon-build --packages-up-to ssc_tactile_hand_bringup
```

## Launch

```bash
ros2 launch ssc_tactile_hand_bringup ssc_tactile_hand.launch.py
```

Hardware operation must be verified on the target hand and RZ/V2H board before deployment.
