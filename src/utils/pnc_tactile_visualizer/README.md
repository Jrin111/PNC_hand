# PNC 3D surface heatmap

Publishes `/tactile/markers` (`visualization_msgs/msg/MarkerArray`) for the Foxglove
3D panel, alongside the existing `/robot_description` and TF tree. Forty-seven
independent colored surface polygons follow the hand's moving links. Five zones
per fingertip and 22 palm zones use the supplied NanoSen manual/Gerber layout.
The hand CAD does not contain the final mounted PNC assembly: patch mounting
positions and rotations remain an installation template to verify on hardware.

The blue → cyan → green → yellow → red scale means increasing **relative PNC
response**, not Newtons or the Inspire hand's internal force reading. A fixed
range avoids changing color just because a different zone is pressed. Set
`color_min`/`color_max` to measured useful limits; per-zone `gain` and `offset`
allow response direction and normalization to be configured without inventing
a Newton calibration. Gray means unknown/unmapped/stale, never zero pressure.

Use `ros2 launch pnc_hand_demo hand_demo.launch.py` for the independent hardware-free
mode. In Foxglove enable `/tactile/markers`, URDF and TF in the same 3D panel, or
import the supplied layout under the repository's `foxglove/` directory.

For real data, copy the appropriate `config/pnc_zones_{left,right}.json`, confirm
each `channel`, mounting geometry and orientation, then set `mapping_verified`
to true. Start the existing motion and tactile stacks, then:

```bash
ros2 run pnc_tactile_visualizer tactile_visualizer --ros-args \
  -p hand_side:=left -p mapping_profile:=verified \
  -p mapping_file:=/absolute/path/to/verified_zones.json \
  -p color_min:=0.0 -p color_max:=1000.0
```

The range above is an example, not a calibrated recommendation. Default profile
`unmapped` displays gray. `demo` explicitly uses `demo_channel` example wiring;
these entries MUST NOT be mistaken for confirmed physical channel assignments.
The complete 54 electrical channels remain available in the diagnostic panel,
including the seven slots outside the 47-zone display.

Input names and values default to `/tactile/tactile_hand_state_broadcaster/names`
and `/values`. The decoder requires names, checks array length, and accepts
NaN as unknown. A 0.5-second stream timeout grays all patches. The repaired real
hardware plugin exports NaN for a chip without a complete successful acquisition,
so its mapped patches become gray while healthy chips continue updating. This
requires rebuilding/deploying `ssc_tactile_hand_ros2_control`; the old checked-in
`install/` plugin still republishes frozen values after failures, which a display
cannot detect from message timestamps alone. Simulated NaN faults exercise the
same unknown-color path but do not prove physical SPI recovery.

The status field `device_health=not_provided_by_hardware` continues to mean there
is no separate hardware health/age interface. NaN communicates measurement
unavailability, not the reason. Successful zero-I retention and auto-tare behavior
remain driver limitations; a finite value is not proof of physical sensor health.

Pure Python verification (without ROS):

```bash
PYTHONPATH=src/utils/pnc_tactile_visualizer \
  python3 -m unittest discover -s src/utils/pnc_tactile_visualizer/test
```
