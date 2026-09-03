#!/usr/bin/env python3
"""Check the installed adapter's profile metadata without issuing hand commands."""
import math
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

import rclpy
from ament_index_python.packages import get_package_prefix
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import Float64, Float64MultiArray, String


def main():
    # Only this temporary adapter is used; every endpoint has a unique namespace.
    os.environ['ROS_DOMAIN_ID'] = '189'
    os.environ['ROS_LOCALHOST_ONLY'] = '1'
    namespace = f'/pnc_profile_width_test_{os.getpid()}'
    binary = Path(get_package_prefix('dexhand_utils')) / 'lib/dexhand_utils/hand_gripper_action_adapter'
    rclpy.init()
    node = rclpy.create_node('observer', namespace=namespace)
    widths, commands = [], []
    latched = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.TRANSIENT_LOCAL)
    node.create_subscription(Float64, f'{namespace}/gripper_max_width',
                             lambda msg: widths.append(msg.data), latched)
    command_subscription = node.create_subscription(
        Float64MultiArray, f'{namespace}/no_hardware_commands',
        lambda msg: commands.append(msg.data), 10)
    publisher = node.create_publisher(String, f'{namespace}/set_grasp_profile', 10)

    with tempfile.TemporaryFile(mode='w+') as output:
        process = subprocess.Popen([
            str(binary), '--ros-args', '-r', f'__ns:={namespace}',
            '-p', f'position_controller_topic:={namespace}/no_hardware_commands',
        ], stdout=output, stderr=subprocess.STDOUT)

        def wait_for(label, predicate, publish=None):
            deadline, next_publish = time.monotonic() + 8.0, 0.0
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'adapter exited during {label}')
                rclpy.spin_once(node, timeout_sec=0.05)
                if predicate():
                    return
                if publish and time.monotonic() >= next_publish:
                    publish()
                    next_publish = time.monotonic() + 0.25
            raise RuntimeError(f'timeout: {label}')

        try:
            wait_for('default full_hand width', lambda: widths and math.isclose(widths[-1], 0.120))
            wait_for('profile subscriber', lambda: publisher.get_subscription_count() == 1)
            wait_for('command observer matched',
                     lambda: command_subscription.get_publisher_count() == 1)
            wait_for('pinch width update', lambda: widths and math.isclose(widths[-1], 0.100),
                     lambda: publisher.publish(String(data='pinch')))
            late_widths = []
            node.create_subscription(Float64, f'{namespace}/gripper_max_width',
                                     lambda msg: late_widths.append(msg.data), latched)
            wait_for('late subscriber receives current width',
                     lambda: late_widths and math.isclose(late_widths[-1], 0.100))
            wait_for('full_hand width restored', lambda: widths and math.isclose(widths[-1], 0.120),
                     lambda: publisher.publish(String(data='full_hand')))
            observation_end = time.monotonic() + 0.5
            while time.monotonic() < observation_end:
                rclpy.spin_once(node, timeout_sec=0.05)
            if commands:
                raise RuntimeError('profile-only check unexpectedly emitted a position command')
            print('PASS: 0.120 -> 0.100 -> 0.120, late subscriber receives 0.100; no position commands')
        except Exception:
            output.seek(0)
            print(output.read())
            raise
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
