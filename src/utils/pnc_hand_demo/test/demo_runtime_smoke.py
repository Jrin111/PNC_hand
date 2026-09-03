#!/usr/bin/env python3
"""Bounded, opt-in integration check for the running left-hand demo only.

Run this source file in a Jazzy environment after hand_demo.launch.py. It writes
only /pnc_demo/tactile_values after observing fresh demo heartbeats and a mock-only
URDF, and restores simulated contacts to zero before exiting. Joint feedback and
TF are observed passively; no joint command publisher is created.
"""

import argparse
import json
import math
import os
from pathlib import Path
import sys
import time
from xml.etree import ElementTree as ET


JOINTS = (
    'thumb_proximal_yaw_joint', 'thumb_proximal_pitch_joint', 'index_proximal_joint',
    'middle_proximal_joint', 'ring_proximal_joint', 'pinky_proximal_joint',
)
KEYS = tuple(
    f'raa{chip}_ch{channel}/{interface}'
    for chip in range(9) for channel in range(6)
    for interface in ('raw_i', 'raw_q', 'value')
)
UNKNOWN = (0.32, 0.34, 0.38, 1.0)
LOW_CONTACT = (0.0, 0.62, 0.88, 1.0)  # 0.25 in the default relative color scale
HIGH_CONTACT = (0.94, 0.12, 0.08, 1.0)  # 1.0 in the default relative color scale


def channel_index(channel):
    chip, sensor = channel.split('_ch')
    return int(chip.removeprefix('raa')) * 6 + int(sensor)


def near(left, right, tolerance=1e-5):
    return len(left) == len(right) and all(
        math.isclose(a, b, rel_tol=tolerance, abs_tol=tolerance)
        for a, b in zip(left, right)
    )


def rotate(quaternion, point):
    """Apply a unit ROS quaternion to an XYZ point without a TF Python dependency."""
    x, y, z, w = quaternion.x, quaternion.y, quaternion.z, quaternion.w
    px, py, pz = point
    tx, ty, tz = 2 * (y * pz - z * py), 2 * (z * px - x * pz), 2 * (x * py - y * px)
    return (px + w * tx + y * tz - z * ty,
            py + w * ty + z * tx - x * tz,
            pz + w * tz + x * ty - y * tx)


def marker_color(marker):
    color = marker.color
    return color.r, color.g, color.b, color.a


def validate_mock_description(description):
    """Reject non-demo descriptions even when Python assertions are disabled."""
    robot = ET.fromstring(description)
    plugins = [(element.text or '').strip()
               for element in robot.findall('./ros2_control/hardware/plugin')]
    if not plugins or any(plugin != 'mock_components/GenericSystem' for plugin in plugins):
        raise RuntimeError(f'Refusing simulated input: expected only GenericSystem hardware, got {plugins}')
    if 'left' not in robot.attrib.get('name', ''):
        raise RuntimeError('Refusing simulated input: expected left-hand URDF')
    if '/meshes/left/' not in description or '/meshes/right/' in description:
        raise RuntimeError('Refusing simulated input: expected exclusively left-hand meshes')
    return robot


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=15.0, help='Seconds per bounded check')
    parser.add_argument('--expected-domain', default='77')
    arguments = parser.parse_args(argv)
    if not math.isfinite(arguments.timeout) or arguments.timeout <= 0:
        parser.error('--timeout must be finite and positive')
    domain = os.environ.get('ROS_DOMAIN_ID', '')
    if (not domain.isdecimal() or not 1 <= int(domain) <= 232
            or domain != arguments.expected_domain):
        parser.error('Set ROS_DOMAIN_ID to the explicit, nonzero --expected-domain (default 77)')

    import rclpy
    from ament_index_python.packages import get_package_share_directory
    from control_msgs.msg import Float64Values, Keys
    from rclpy.node import Node
    from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
    from sensor_msgs.msg import JointState
    from std_msgs.msg import Bool, Float64MultiArray, String
    from tf2_msgs.msg import TFMessage
    from visualization_msgs.msg import Marker, MarkerArray

    class DemoSmoke(Node):
        def __init__(self):
            super().__init__('pnc_demo_runtime_smoke')
            self.messages, self.received, self.counts, self.transforms = {}, {}, {}, {}
            self.authorized_description = None
            self.armed = False
            reliable = QoSProfile(depth=20, reliability=ReliabilityPolicy.RELIABLE)
            latched = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                                 durability=DurabilityPolicy.TRANSIENT_LOCAL)
            stream = QoSProfile(depth=50, reliability=ReliabilityPolicy.BEST_EFFORT)
            for key, message_type, topic, qos in (
                ('enabled', Bool, '/pnc_demo/enabled', latched),
                ('names', Keys, '/tactile/tactile_hand_state_broadcaster/names', latched),
                ('values', Float64Values, '/tactile/tactile_hand_state_broadcaster/values', stream),
                ('markers', MarkerArray, '/tactile/markers', latched),
                ('description', String, '/robot_description', latched),
                ('joints', JointState, '/joint_states', stream),
            ):
                self.create_subscription(message_type, topic,
                                         lambda message, key=key: self.record(key, message), qos)
            self.create_subscription(TFMessage, '/tf', self.on_tf, stream)
            self.create_subscription(TFMessage, '/tf_static', self.on_tf, latched)
            self.tactile_publisher = self.create_publisher(
                Float64MultiArray, '/pnc_demo/tactile_values', reliable)

        def record(self, key, message):
            self.messages[key] = message
            self.received[key] = time.monotonic()
            self.counts[key] = self.counts.get(key, 0) + 1

        def on_tf(self, message):
            for transform in message.transforms:
                self.transforms[transform.child_frame_id] = transform

        def wait(self, label, predicate, publish=None, timeout=None):
            deadline = time.monotonic() + (timeout or arguments.timeout)
            next_publish = 0.0
            while rclpy.ok() and time.monotonic() < deadline:
                rclpy.spin_once(self, timeout_sec=0.05)
                now = time.monotonic()
                if publish is not None and now >= next_publish:
                    self.require_demo()
                    publish()
                    next_publish = now + 0.15
                if predicate():
                    print(f'PASS {label}', flush=True)
                    return
            raise AssertionError(f'Timed out after {timeout or arguments.timeout:g}s: {label}')

        def demo_alive(self):
            return (self.counts.get('enabled', 0) >= 2
                    and self.messages['enabled'].data is True
                    and time.monotonic() - self.received['enabled'] < 2.5)

        def require_demo(self):
            if not self.armed or not self.demo_alive():
                raise RuntimeError('Refusing simulated input: no fresh, verified demo heartbeat')
            if self.messages['description'].data != self.authorized_description:
                raise RuntimeError('Refusing simulated input: robot_description changed after mock verification')
            self.reject_multiple_description_sources()

        def reject_multiple_description_sources(self):
            # Discovery is not a safety proof; this only rejects a known mixed graph.
            if self.count_publishers('/robot_description') > 1:
                raise RuntimeError('Refusing simulated input: multiple robot_description publishers discovered')

        def publish_tactile(self, values):
            self.require_demo()
            self.tactile_publisher.publish(Float64MultiArray(data=values))

        def values_match(self, desired):
            message = self.messages.get('values')
            if message is None or len(message.values) != 162:
                return False
            return all(
                (all(math.isnan(value) for value in message.values[index * 3:index * 3 + 3])
                 if math.isnan(target) else near(message.values[index * 3:index * 3 + 3], (target, 0.0, target)))
                for index, target in enumerate(desired)
            )

        def markers_by_name(self):
            message = self.messages.get('markers')
            return {marker.ns: marker for marker in message.markers} if message else {}

        def colors_match(self, desired):
            markers = self.markers_by_name()
            return all(name in markers and near(marker_color(markers[name]), color)
                       for name, color in desired.items())

        def joint_positions_available(self):
            message = self.messages.get('joints')
            if message is None or len(message.name) != len(message.position):
                return False
            actual = dict(zip(message.name, message.position))
            return all(joint in actual and math.isfinite(actual[joint]) for joint in JOINTS)

        def point_in_world(self, marker):
            if not marker.points:
                raise AssertionError(f'{marker.ns}: no surface triangles')
            point = tuple(sum(getattr(vertex, axis) for vertex in marker.points) / len(marker.points)
                          for axis in ('x', 'y', 'z'))
            point = rotate(marker.pose.orientation, point)
            point = tuple(value + getattr(marker.pose.position, axis)
                          for value, axis in zip(point, ('x', 'y', 'z')))
            frame, visited = marker.header.frame_id, set()
            while frame != 'world':
                if frame in visited or frame not in self.transforms:
                    raise KeyError(f'No complete world transform for {marker.header.frame_id}')
                visited.add(frame)
                transform = self.transforms[frame]
                point = rotate(transform.transform.rotation, point)
                point = tuple(value + getattr(transform.transform.translation, axis)
                              for value, axis in zip(point, ('x', 'y', 'z')))
                frame = transform.header.frame_id
            return point

        def all_frames_resolve(self, links):
            markers = self.markers_by_name()
            if len(markers) != 47:
                return False
            try:
                for marker in markers.values():
                    if marker.header.frame_id not in links:
                        return False
                    self.point_in_world(marker)
            except KeyError:
                return False
            return True

        def restore(self):
            if not self.armed:
                return
            if not self.demo_alive():
                print('RESTORE SKIPPED: demo heartbeat expired; no further simulated input sent', file=sys.stderr)
                return
            tactile_zero = [0.0] * 54
            self.wait('restore simulated tactile input to zero',
                      lambda: self.values_match(tactile_zero),
                      publish=lambda: self.publish_tactile(tactile_zero), timeout=5.0)

        def run(self):
            self.wait('live demo heartbeat (a latched true alone is insufficient)', self.demo_alive)
            self.wait('robot_description and simulated tactile subscriber', lambda:
                      'description' in self.messages
                      and self.tactile_publisher.get_subscription_count() > 0)
            description = self.messages['description'].data
            robot = validate_mock_description(description)
            self.reject_multiple_description_sources()
            self.authorized_description, self.armed = description, True
            print('PASS left-hand URDF contains only GenericSystem hardware', flush=True)

            self.wait('162 names and values in exact 54-channel production order', lambda:
                      'names' in self.messages and tuple(self.messages['names'].keys) == KEYS
                      and 'values' in self.messages and len(self.messages['values'].values) == 162)
            config = json.loads((Path(get_package_share_directory('pnc_tactile_visualizer')) /
                                 'config/pnc_zones_left.json').read_text())
            zones = config['zones']
            assert len(zones) == 47
            self.wait('47 unique frame-locked triangle patches attached to valid TF chains',
                      lambda: self.all_frames_resolve({link.attrib['name'] for link in robot.findall('link')}))
            markers = self.markers_by_name()
            assert set(markers) == {'pnc/' + zone['id'] for zone in zones}
            assert all(marker.frame_locked and marker.type == Marker.TRIANGLE_LIST
                       and len(marker.points) >= 3 and len(marker.points) % 3 == 0
                       for marker in markers.values())

            first = zones[0]
            first_index = channel_index(first['demo_channel'])
            second = next(zone for zone in zones if channel_index(zone['demo_channel']) // 6 != first_index // 6)
            second_index = channel_index(second['demo_channel'])
            first_name, second_name = 'pnc/' + first['id'], 'pnc/' + second['id']
            contacts = [0.0] * 54
            contacts[first_index], contacts[second_index] = 0.25, 1.0
            self.wait('two simultaneous contacts retain different per-patch colors', lambda:
                      self.values_match(contacts)
                      and self.colors_match({first_name: LOW_CONTACT, second_name: HIGH_CONTACT}),
                      publish=lambda: self.publish_tactile(contacts))

            fault = contacts.copy()
            chip_start = first_index // 6 * 6
            fault[chip_start:chip_start + 6] = [math.nan] * 6
            expected_colors = {'pnc/' + zone['id']: UNKNOWN for zone in zones
                               if channel_index(zone['demo_channel']) // 6 == first_index // 6}
            expected_colors[second_name] = HIGH_CONTACT
            self.wait('NaN chip fault grays affected patches and preserves a healthy contact', lambda:
                      self.values_match(fault) and self.colors_match(expected_colors),
                      publish=lambda: self.publish_tactile(fault))
            self.wait('finite recovery restores contact colors without resetting the healthy contact', lambda:
                      self.values_match(contacts)
                      and self.colors_match({first_name: LOW_CONTACT, second_name: HIGH_CONTACT}),
                      publish=lambda: self.publish_tactile(contacts))

            self.wait('six finite joint positions observed passively', self.joint_positions_available)
            joints_message = self.messages['joints']
            assert all(not math.isfinite(value) for value in joints_message.velocity), 'Mock velocity must be unavailable'
            assert all(not math.isfinite(value) for value in joints_message.effort), 'Mock effort must be unavailable'
            print('PASS passive joint feedback: no measured mock velocity/effort is fabricated', flush=True)
            print('NOT RUN joint motion or command tracking; joint feedback and TF checks are passive', flush=True)

    rclpy.init()
    node = DemoSmoke()
    failed = None
    try:
        node.run()
    except Exception as exc:
        failed = exc
    finally:
        try:
            node.restore()
        except Exception as exc:
            failed = failed or exc
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    if failed is not None:
        print(f'FAIL {type(failed).__name__}: {failed}', file=sys.stderr, flush=True)
        return 1
    print('PASS tactile ROS demo smoke check (passive joint/TF checks; no motor commands or physical validation)', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
