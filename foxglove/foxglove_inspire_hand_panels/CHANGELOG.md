## 1.1.1 — Short Console panel layout

- Keep joint rows at their readable content height and scroll short Console panels instead of overlapping sliders, feedback and presets.
- Scope the layout correction to Hand Console; Force, Gripper and PNC data behavior are unchanged.

## 1.1.0 — PNC hand integration

- Replace six-contact tactile conversion/panel with 54-channel name-based PNC diagnostics.
- Preserve unknown values, clear stale display state, and gate manual simulation input on a fresh simulator heartbeat.
- Preserve the three actuator panels and joint converter; missing measured fields now stay Unknown.
- Supply ROS 2 command datatype definitions and add decoder/authorization regression tests.

# Changelog

## 0.1.0

- Initial scaffold.
- Topic converters: `/dynamic_joint_states` -> `/inspire_hand/joints`,
  `/tactile_glove_state_broadcaster/{names,values}` -> `/tactile_glove/pads`.
