# PNC hand demo

Software-only demo: RH56E2 `mock_components/GenericSystem`, 54 simulated tactile
channels, 3D tactile markers, and one Foxglove bridge. It does not start the SPI
driver or a gesture node. Mock joint position follows commands; velocity and
force measurements are unavailable and are omitted by the mock broadcaster.

Build in a ROS 2 Jazzy workspace with the normal motion and Foxglove dependencies:

```bash
colcon build --packages-up-to pnc_hand_demo --symlink-install
source install/setup.bash
ros2 launch pnc_hand_demo hand_demo.launch.py
```

All demo child processes run in `ROS_DOMAIN_ID=77`, including the motion launch,
spawners and bridge. Choose a domain unused by physical hardware. The launching
shell's environment is not changed; CLI tools need `ROS_DOMAIN_ID=77` explicitly.
Use `demo_domain_id:=78`, `hand_side:=right`, `scenario:=manual`, or
`foxglove_port:=8766` as needed. Connect Foxglove to `ws://localhost:8765` (or the
demo machine's address). Uppercase `.STL` hand meshes are allowed by the bridge.

`scenario:=sweep` repeats a contact over electrical channel order every 18 s.
The visualizer receives `mapping_profile:=demo`: positions are provisional,
not a confirmed board-to-hand wiring map. Relative values normally span 0–1;
they are not Newtons. This launch sends no automatic motor motion commands.

The source publishes the same schema and electrical order as production:

| Topic | Type | Meaning |
| --- | --- | --- |
| `/tactile/tactile_hand_state_broadcaster/names` | `control_msgs/msg/Keys` | 162 keys, transient-local |
| `/tactile/tactile_hand_state_broadcaster/values` | `control_msgs/msg/Float64Values` | 40 Hz; `raw_i, raw_q, value` for `raa0_ch0` through `raa8_ch5` |
| `/pnc_demo/enabled` | `std_msgs/msg/Bool` | `true`, transient-local and 1 Hz heartbeat, only for this ROS domain |
| `/pnc_demo/tactile_values` | `std_msgs/msg/Float64MultiArray` | Manual input, exactly 54 relative values |

A valid manual array takes over the sweep until restart; `manual` starts at zero.
Malformed arrays leave the previous scenario and values intact. All nonfinite
inputs become NaN in all three interfaces for that channel; six NaNs can model
a chip outage, and a later finite array restores its values. Unchanged manual
values intentionally remain held until another command. Message timestamps
describe publication time, not sensor acquisition or chip health. A consumer
must expire the demo heartbeat if the source stops; a latched `true` alone is
not proof that simulation is still running.

Synthetic `raw_i` equals the input and `raw_q` is zero. These placeholders test
the complete 162-interface message path; they do not emulate ADC physics.

Example manual contact on channel 0 (use the same domain as the launch):

```bash
ROS_DOMAIN_ID=77 python3 - <<'PY'
import rclpy
from std_msgs.msg import Float64MultiArray
rclpy.init()
node = rclpy.create_node('manual_tactile_example')
pub = node.create_publisher(Float64MultiArray, '/pnc_demo/tactile_values', 10)
msg = Float64MultiArray(data=[0.8] + [0.0] * 53)
def send():
    pub.publish(msg)
    rclpy.shutdown()
node.create_timer(1.0, send)
try:
    rclpy.spin(node)
finally:
    node.destroy_node()
PY
```

Run the deterministic message-model tests without ROS, from this package:

```bash
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=. python3 -m unittest discover -s test -v
```

These tests check data ordering, invalid input, NaN fault/recovery and repeatable
scenarios. They do not verify ROS discovery, the loaded controller plugins,
Foxglove rendering or physical sensing; those require the appropriate runtime.

With the default left-hand demo running, execute the opt-in ROS integration check
from the workspace root in another sourced terminal:

```bash
ROS_DOMAIN_ID=77 python3 src/utils/pnc_hand_demo/test/demo_runtime_smoke.py
```

The script waits for fresh demo heartbeats and verifies the mock-only URDF before
sending commands. It checks 162 named interfaces, 47 patches and their TF chains,
simultaneous independent colors, NaN chip faults and recovery, then joint position
feedback and patch motion through TF. It restores tactile and mock joint commands
to zero. Each check has a 15 s timeout (`--timeout` can change it); another demo
domain also needs `--expected-domain`. This verifies ROS messages and mock behavior,
not the final Foxglove pixels, physical force, or real motor arrival.
