# Dex Hand Utils

Utility nodes for DexHand gripper action adapter with configurable joint mapping.

## hand_gripper_action_adapter

Provides a `control_msgs/action/ParallelGripperCommand` action server interface that converts gripper commands to position commands for the 6-joint dexterous hand. This allows standard gripper action clients to work with the dexterous hand using configurable YAML-based joint mapping.

### Features
- Action server compatible with standard gripper action clients
- Topic subscriber for simple gripper commands (no action feedback)
- Converts parallel gripper commands to 6-joint hand positions using YAML configuration
- Linear interpolation between configured mapping points for smooth motion
- Configurable execution duration for different hand speeds
- YAML-based mapping allows customization of grip patterns
- Automatic max_gripper_width determination from configuration

### Joint Mapping

The adapter maps gripper width commands to the following 6 joints:
1. `thumb_proximal_yaw_joint` - Thumb yaw movement
2. `thumb_proximal_pitch_joint` - Thumb pitch/flex movement
3. `index_proximal_joint` - Index finger flex
4. `middle_proximal_joint` - Middle finger flex
5. `ring_proximal_joint` - Ring finger flex
6. `pinky_proximal_joint` - Pinky finger flex

### Parameters
- `action_server_name` (string, default: `hand_gripper_cmd`) - Name of the action server
- `gripper_command_topic` (string, default: `hand_gripper_command`) - Topic for simple gripper commands
- `position_controller_topic` (string, default: `/dexhand_position_controller/commands`) - Topic for position controller commands
- `mapping_config_file` (string, default: `rh56e2_gripper_joint_mapping.yaml`) - multi-profile YAML mapping file (holds every grasp profile)
- `profile_topic` (string, default: `set_grasp_profile`) - topic to switch the active grasp profile at runtime
- `execution_duration` (double, default: `2.0`) - Duration for hand action execution (seconds)

### Topics
- `~/hand_gripper_cmd` (control_msgs/action/ParallelGripperCommand) - Action server for gripper commands
- `~/hand_gripper_command` (control_msgs/msg/GripperCommand) - Subscriber for simple gripper commands
- `set_grasp_profile` (std_msgs/msg/String) - Subscriber to switch the active grasp profile at runtime (payload = profile name, e.g. `three_fingers`)
- Position controller topic (std_msgs/Float64MultiArray) - Position commands to hand controller

### Configuration Files

The mapping is a single **multi-profile** YAML — one file holding every grasp profile, keyed by name:

- `config/rh56e2_gripper_joint_mapping.yaml` - RH56E2, profiles: `full_hand` (max_width 0.120m),
  `three_fingers` (0.120m), `pinch` (0.100m). `default_profile: full_hand`.
- `config/rh56_gripper_joint_mapping.yaml` - RH56, profiles: `full_hand` (max_width 0.080m),
  `three_fingers` (0.060m), `pinch` (0.060m). `default_profile: full_hand`.

The YAML mapping format:
```yaml
default_profile: full_hand     # profile used until one is selected on set_grasp_profile
profiles:
  three_fingers:
    description: "3-finger tripod grasp (thumb + index + middle)"
    mapping:                   # joint_positions order: [thumb_yaw, thumb_pitch, index, middle, ring, pinky]
      - {gripper_width: 0.000, joint_positions: [1.57, 0.29, 0.79, 0.73, 1.43, 1.43]}  # fully closed
      - {gripper_width: 0.120, joint_positions: [1.57, 0.01, 0.09, 0.05, 1.43, 1.43]}  # max_width = last entry
  pinch:
    description: "2-finger pinch grasp (thumb + index)"
    mapping:
      - {gripper_width: 0.000, joint_positions: [1.48, 0.31, 0.73, 1.43, 1.43, 1.43]}
      - {gripper_width: 0.100, joint_positions: [1.48, 0.00, 0.43, 1.43, 1.43, 1.43]}
```

Within a profile, `gripper_width` → joint angles is linearly interpolated; `max_gripper_width` is the last
(widest) entry. Switch the active profile at runtime by publishing its name on `set_grasp_profile`.

## Usage

### Hand Gripper Action Adapter

```bash
# Run the hand gripper action adapter with default parameters
ros2 run dexhand_utils hand_gripper_action_adapter

# Run with custom parameters
ros2 run dexhand_utils hand_gripper_action_adapter --ros-args \
  -p action_server_name:=my_hand_gripper_cmd \
  -p gripper_command_topic:=my_hand_gripper_command \
  -p execution_duration:=3.0

# Switch the active grasp profile at runtime (no restart): full_hand / three_fingers / pinch
ros2 topic pub -1 /set_grasp_profile std_msgs/msg/String "{data: 'three_fingers'}"

# Send gripper commands via action (with feedback)
ros2 action send_goal /hand_gripper_cmd control_msgs/action/ParallelGripperCommand "{command: {position: [0.03], effort: [10.0]}}"

# Send simple gripper commands via topic (no feedback)
ros2 topic pub /hand_gripper_command control_msgs/msg/GripperCommand "{position: 0.03, effort: 10.0}"

# Close hand via topic
ros2 topic pub /hand_gripper_command control_msgs/msg/GripperCommand "{position: 0.0, effort: 5.0}"
```
