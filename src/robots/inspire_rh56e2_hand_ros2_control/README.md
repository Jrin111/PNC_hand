# inspire_rh56e2_hand_ros2_control

ROS 2 package that provides a ros2_control hardware interface for the Inspire RH56E2 6-DOF dexterous hand. This package contains the hardware interface implementation that communicates with the physical hand hardware via serial connection.

## Features
- ros2_control hardware interface plugin (exported via `hardware_interface_plugin.xml`)
- Hardware interface implementation for serial communication with the hand
- Position command/state and velocity state interfaces for all 6 joints
- Force threshold command interface (0–3000 g per joint) for grasp force control
- Motion mode command interface (0/1/2 per joint) to switch grasp behaviour at runtime
- Force state interface reporting actual finger forces (±4000 g) via `/dynamic_joint_states`
- ros2_control URDF macro for hardware interface integration
- Pre-built RS-485 serial frames with change detection for efficient bus utilisation

## Related Packages
- **inspire_rh56e2_hand_bringup**: Contains launch files, controller configurations, robot URDF descriptions, and test scripts for operating the hand
- **inspire_rh56e2_hand_description**: Contains the hand's visual and collision meshes, joint definitions

## Package layout
- `include/` / `src/`: Hardware interface implementation (`inspire_rh56e2_hand_hardware_interface`)
- `config/`: Controller configuration files (force threshold controller, controller manager)
- `urdf/`: ros2_control URDF macro for hardware interface integration
- `hardware_interface_plugin.xml`: Plugin description file for the hardware interface

## Prerequisites
- ROS 2 (Jazzy or newer) with `ros2_control` ecosystem
- A colcon workspace (e.g., `~/ros2_ws`)
- Serial interface support for hardware communication

## Usage
This package provides the hardware interface that should be used with the `inspire_rh56e2_hand_bringup` package for launching and controlling the hand.

To use this hardware interface in your hand system, you'll typically launch one of the bringup configurations:

```bash
# For joint position control
ros2 launch inspire_rh56e2_hand_bringup inspire_rh56e2_hand_joint_position_control.launch.py

# For joint trajectory control  
ros2 launch inspire_rh56e2_hand_bringup inspire_rh56e2_hand_joint_trajectory_control.launch.py
```

After launching, you can introspect the hardware interface:
```bash
ros2 control list_hardware_interfaces
ros2 control list_controllers
```

Set force thresholds (grams, 0–3000) per finger:
```bash
ros2 topic pub -1 /inspire_rh56e2_hand_force_threshold_controller/commands \
  std_msgs/msg/Float64MultiArray "{data: [1000, 1000, 1000, 1000, 1000, 1000]}"
```

Set the motion mode per finger at runtime (`0` = Speed/Force Protection, `1` = Force Control,
`2` = Load Retention). For example, switch all fingers to closed-loop force control (e.g. to
gently grasp a fragile object such as an egg):
```bash
ros2 topic pub -1 /inspire_rh56e2_hand_motion_mode_controller/commands \
  std_msgs/msg/Float64MultiArray "{data: [1, 1, 1, 1, 1, 1]}"
```
The hardware interface defaults all joints to mode `0` at activation; the mode register is only
re-written when a commanded value changes.

## URDF Integration
The ros2_control hardware interface macro is provided in `urdf/`:
- `inspire_rh56e2_hand_macro.ros2_control.xacro`: ros2_control hardware interface, transmissions, and interfaces

To include the hardware interface in your hand URDF:
```xml
<xacro:include filename="$(find inspire_rh56e2_hand_ros2_control)/urdf/inspire_rh56e2_hand_macro.ros2_control.xacro"/>
<xacro:inspire_rh56e2_hand_ros2_control
  name="inspire_rh56e2_hand"
  serial_port="/dev/ttyUSB0"
  baudrate="115200"
  use_mock_hardware="false"/>
```
Adjust arguments as needed (see the xacro file for available parameters).

## Configuration Options
The hardware interface supports the following configuration options:
- `serial_port`: Serial device path for hardware communication (e.g., "/dev/ttyUSB0")
- `baudrate`: Serial communication baudrate (default: "115200")
- `use_mock_hardware`: Set to "true" for simulation/testing without physical hardware

## Joint Interfaces
The hardware interface exposes 6 joints:
- `thumb_proximal_yaw_joint`, `thumb_proximal_pitch_joint`
- `index_proximal_joint`, `middle_proximal_joint`, `ring_proximal_joint`, `pinky_proximal_joint`

Each joint provides the following interfaces:

| Interface | Direction | Type | Unit | Range |
|-----------|-----------|------|------|-------|
| `position` | command / state | `double` | rad | 0 – joint limit |
| `velocity` | state | `double` | rad/s | — |
| `force_threshold` | command | `double` | grams | 0 – 3000 |
| `motion_mode` | command | `double` | enum | 0 – 2 |
| `force` | state | `double` | grams | −4000 – 4000 |

- The **force_threshold command** interface sets the force control threshold per finger (register FORCE_SET, default 1000 g). It is driven by a `ForwardCommandController` (`inspire_rh56e2_hand_force_threshold_controller`).
- The **motion_mode command** interface selects the per-finger motion mode (register MOTION_MODE, 1625–1630): `0` = Speed/Force Protection (stop at target angle or force limit, the default), `1` = Force Control (closed-loop force maintenance), `2` = Load Retention (continuous current static hold). It is driven by a `ForwardCommandController` (`inspire_rh56e2_hand_motion_mode_controller`). Values are rounded and clamped to `0–2`; the register is only written when the commanded mode changes.
- The **force state** interface reports actual finger contact force (register FORCE_ACT) and is published on the `/dynamic_joint_states` topic by `joint_state_broadcaster`.

## Development
For detailed usage examples, launch configurations, controller setups, and test scripts, see the `inspire_rh56e2_hand_bringup` package.
