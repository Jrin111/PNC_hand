"""Exercise the opt-in script without connecting to a ROS graph or hardware."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name('demo_runtime_smoke.py')
SPEC = importlib.util.spec_from_file_location('demo_runtime_smoke', SCRIPT)
smoke = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(smoke)
TACTILE_TOPIC = '/pnc_demo/tactile_values'
MOCK_DESCRIPTION = '''<robot name="inspire_left">
  <link name="world"/><link name="left_base"/>
  <mesh filename="package://hand/meshes/left/base.STL"/>
  <ros2_control><hardware><plugin>mock_components/GenericSystem</plugin></hardware></ros2_control>
</robot>'''


class Message(SimpleNamespace):
    TRIANGLE_LIST = 11


class FakeRos:
    """In-memory callbacks and publishers; no rclpy installation is loaded."""

    def __init__(self, description=MOCK_DESCRIPTION, fail_on_fault=False,
                 description_publishers=1, heartbeat=True):
        self.description = description
        self.fail_on_fault = fail_on_fault
        self.description_publishers = description_publishers
        self.heartbeat = heartbeat
        self.values = [0.0] * 54
        self.created_topics = []
        self.publications = []
        self.active = True
        self.destroyed = False
        self.subscriptions = {}
        self.zones = [{'id': str(index), 'demo_channel': f'raa{index // 6}_ch{index % 6}',
                       'frame_id': 'left_base'} for index in range(47)]
        runtime = self

        class Node:
            def __init__(self, name):
                runtime.node_name = name

            def create_subscription(self, message_type, topic, callback, qos):
                runtime.subscriptions[topic] = callback

            def create_publisher(self, message_type, topic, qos):
                runtime.created_topics.append(topic)

                class Publisher:
                    def get_subscription_count(self):
                        return 1

                    def publish(self, message):
                        runtime.publications.append((topic, list(message.data)))
                        runtime.values = list(message.data)
                        if runtime.fail_on_fault and any(math.isnan(value) for value in message.data):
                            runtime.fail_on_fault = False
                            raise RuntimeError('Injected failure after simulated fault publication')

                return Publisher()

            def count_publishers(self, topic):
                if topic != '/robot_description':
                    raise ValueError(topic)
                return runtime.description_publishers

            def destroy_node(self):
                runtime.destroyed = True

        self.node_type = Node

    def spin_once(self, node, timeout_sec):
        origin = Message(x=0.0, y=0.0, z=0.0)
        identity = Message(x=0.0, y=0.0, z=0.0, w=1.0)
        markers = []
        for zone in self.zones:
            value = self.values[smoke.channel_index(zone['demo_channel'])]
            color = (smoke.UNKNOWN if not math.isfinite(value) else
                     smoke.LOW_CONTACT if value == 0.25 else
                     smoke.HIGH_CONTACT if value == 1.0 else (0.0, 0.0, 0.0, 1.0))
            markers.append(Message(ns='pnc/' + zone['id'], frame_locked=True,
                                   type=Message.TRIANGLE_LIST, points=[origin] * 3,
                                   color=Message(**dict(zip(('r', 'g', 'b', 'a'), color))),
                                   pose=Message(position=origin, orientation=identity),
                                   header=Message(frame_id='left_base')))
        values = [item for value in self.values
                  for item in ((math.nan,) * 3 if not math.isfinite(value) else (value, 0.0, value))]
        messages = {
            '/pnc_demo/enabled': Message(data=self.heartbeat),
            '/tactile/tactile_hand_state_broadcaster/names': Message(keys=smoke.KEYS),
            '/tactile/tactile_hand_state_broadcaster/values': Message(values=values),
            '/tactile/markers': Message(markers=markers),
            '/robot_description': Message(data=self.description),
            # Nonzero joint feedback ensures the script does not demand zero or a motion target.
            '/joint_states': Message(name=smoke.JOINTS, position=[0.37] * 6, velocity=[], effort=[]),
            '/tf': Message(transforms=[Message(child_frame_id='left_base',
                                              header=Message(frame_id='world'),
                                              transform=Message(translation=origin, rotation=identity))]),
            '/tf_static': Message(transforms=[]),
        }
        for topic, callback in self.subscriptions.items():
            callback(messages[topic])

    def modules(self, share):
        def module(name, **attributes):
            result = ModuleType(name)
            result.__dict__.update(attributes)
            return result

        result = {
            'rclpy': module('rclpy', init=lambda: None, ok=lambda: self.active,
                            shutdown=lambda: setattr(self, 'active', False), spin_once=self.spin_once),
            'rclpy.node': module('rclpy.node', Node=self.node_type),
            'rclpy.qos': module('rclpy.qos', QoSProfile=Message,
                                DurabilityPolicy=Message(TRANSIENT_LOCAL=1),
                                ReliabilityPolicy=Message(RELIABLE=1, BEST_EFFORT=2)),
            'ament_index_python.packages': module('ament_index_python.packages',
                                                   get_package_share_directory=lambda name: str(share)),
        }
        for package, names in (
            ('control_msgs', ('Float64Values', 'Keys')),
            ('sensor_msgs', ('JointState',)),
            ('std_msgs', ('Bool', 'Float64MultiArray', 'String')),
            ('tf2_msgs', ('TFMessage',)),
            ('visualization_msgs', ('Marker', 'MarkerArray')),
        ):
            result[package] = module(package)
            result[package + '.msg'] = module(package + '.msg', **dict.fromkeys(names, Message))
        result['ament_index_python'] = module('ament_index_python')
        return result

    def run(self, domain='77'):
        output, errors = io.StringIO(), io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            share = Path(directory)
            (share / 'config').mkdir()
            (share / 'config/pnc_zones_left.json').write_text(json.dumps({'zones': self.zones}))
            with patch.dict(sys.modules, self.modules(share)), patch.dict(os.environ, {'ROS_DOMAIN_ID': domain}):
                with redirect_stdout(output), redirect_stderr(errors):
                    result = smoke.main(['--timeout', '0.02'])
        return result, output.getvalue(), errors.getvalue()


class DemoRuntimeSmokeTests(unittest.TestCase):
    def assert_tactile_only(self, runtime):
        self.assertEqual(runtime.created_topics, [TACTILE_TOPIC])
        self.assertTrue(all(topic == TACTILE_TOPIC and len(values) == 54
                            for topic, values in runtime.publications))
        self.assertTrue(runtime.destroyed)
        self.assertFalse(runtime.active)

    def test_success_observes_nonzero_joints_and_restores_only_tactile(self):
        runtime = FakeRos()
        result, output, errors = runtime.run()
        self.assertEqual(result, 0, errors)
        self.assert_tactile_only(runtime)
        self.assertGreaterEqual(len(runtime.publications), 4)
        self.assertEqual(runtime.publications[-1][1], [0.0] * 54)
        self.assertIn('NOT RUN joint motion or command tracking', output)
        self.assertIn('six finite joint positions observed passively', output)

    def test_failure_cleanup_restores_only_simulated_tactile(self):
        runtime = FakeRos(fail_on_fault=True)
        result, output, errors = runtime.run()
        self.assertEqual(result, 1)
        self.assertIn('Injected failure', errors)
        self.assert_tactile_only(runtime)
        self.assertEqual(runtime.publications[-1][1], [0.0] * 54)
        self.assertIn('restore simulated tactile input to zero', output)

    def test_real_hardware_description_never_publishes(self):
        runtime = FakeRos(description=MOCK_DESCRIPTION.replace(
            'mock_components/GenericSystem', 'inspire_rh56e2_hand/RealHardware'))
        result, output, errors = runtime.run()
        self.assertEqual(result, 1)
        self.assertIn('expected only GenericSystem hardware', errors)
        self.assert_tactile_only(runtime)
        self.assertEqual(runtime.publications, [])

    def test_optimized_python_still_rejects_real_hardware(self):
        result = subprocess.run(
            [sys.executable, '-O', '-m', 'unittest',
             'test_demo_runtime_smoke.DemoRuntimeSmokeTests.test_real_hardware_description_never_publishes'],
            cwd=SCRIPT.parent, capture_output=True, text=True, check=False,
            env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('OK', result.stderr)

    def test_discovered_multiple_descriptions_never_publish(self):
        runtime = FakeRos(description_publishers=2)
        result, output, errors = runtime.run()
        self.assertEqual(result, 1)
        self.assertIn('multiple robot_description publishers discovered', errors)
        self.assert_tactile_only(runtime)
        self.assertEqual(runtime.publications, [])

    def test_missing_live_heartbeat_never_publishes(self):
        runtime = FakeRos(heartbeat=False)
        result, output, errors = runtime.run()
        self.assertEqual(result, 1)
        self.assertIn('live demo heartbeat', errors)
        self.assert_tactile_only(runtime)
        self.assertEqual(runtime.publications, [])

    def test_wrong_domain_rejected_before_ros_node_exists(self):
        runtime = FakeRos()
        with self.assertRaises(SystemExit) as error:
            runtime.run(domain='0')
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(runtime.created_topics, [])
        self.assertEqual(runtime.publications, [])


if __name__ == '__main__':
    unittest.main()
