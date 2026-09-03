# Upstream provenance

This directory is an unmodified sparse export of the
`state_interfaces_broadcaster` package from the official
[`ros-controls/ros2_controllers`](https://github.com/ros-controls/ros2_controllers) repository.

- ROS distribution branch: `jazzy`
- Upstream commit: `2d014a94b8ce6c6d76d6719872bba218c89cc3bf`
- Package version: `4.42.1`
- Retrieved: `2026-09-02`
- License declared by upstream package: Apache License 2.0

The package is included in this workspace so the AArch64 install tree contains the controller
plugin required by `ssc_tactile_hand_bringup`; previously it was only an undeployed runtime
dependency.
