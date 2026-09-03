"""ROS entry point. Importing the model or this module does not require ROS."""

import math
import os
import time

from .model import DemoModel, INTERFACE_KEYS


NAMES_TOPIC = '/tactile/tactile_hand_state_broadcaster/names'
VALUES_TOPIC = '/tactile/tactile_hand_state_broadcaster/values'
INPUT_TOPIC = '/pnc_demo/tactile_values'
ENABLED_TOPIC = '/pnc_demo/enabled'


def main(args=None):
    import rclpy
    from control_msgs.msg import Float64Values, Keys
    from rclpy.node import Node
    from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
    from std_msgs.msg import Bool, Float64MultiArray

    domain = os.environ.get('ROS_DOMAIN_ID', '')
    if not domain.isdecimal() or not 1 <= int(domain) <= 232:
        raise RuntimeError('Demo requires an explicit ROS_DOMAIN_ID in 1..232; use the demo launch (77).')

    class TactileDemoSource(Node):
        def __init__(self):
            super().__init__('tactile_demo_source')
            self.declare_parameter('scenario', 'sweep')
            self.declare_parameter('publish_rate', 40.0)
            rate = float(self.get_parameter('publish_rate').value)
            if not math.isfinite(rate) or not 1.0 <= rate <= 200.0:
                raise ValueError('publish_rate must be between 1 and 200 Hz')
            self.model = DemoModel(self.get_parameter('scenario').value)
            self.start_time = time.monotonic()
            latched = QoSProfile(
                depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL,
                reliability=ReliabilityPolicy.RELIABLE,
            )
            self.names_publisher = self.create_publisher(Keys, NAMES_TOPIC, latched)
            self.values_publisher = self.create_publisher(Float64Values, VALUES_TOPIC, 10)
            self.enabled_publisher = self.create_publisher(Bool, ENABLED_TOPIC, latched)
            self.input_subscription = self.create_subscription(
                Float64MultiArray, INPUT_TOPIC, self.on_manual_input, 10,
            )
            names = Keys()
            names.header.stamp = self.get_clock().now().to_msg()
            names.keys = list(INTERFACE_KEYS)
            self.names_publisher.publish(names)
            self.publish_enabled()
            self.sample_timer = self.create_timer(1.0 / rate, self.publish_sample)
            self.heartbeat_timer = self.create_timer(1.0, self.publish_enabled)
            self.get_logger().warning(
                f'SIMULATED tactile data in ROS domain {domain}; scenario={self.model.scenario}. '
                'Manual input takes over the sweep. No physical force or chip-health measurement.'
            )

        def on_manual_input(self, message):
            try:
                self.model.set_manual_values(message.data)
            except ValueError as exc:
                self.get_logger().warning(f'Rejected tactile demo input: {exc}')

        def publish_enabled(self):
            message = Bool()
            message.data = True
            self.enabled_publisher.publish(message)

        def publish_sample(self):
            message = Float64Values()
            message.header.stamp = self.get_clock().now().to_msg()
            message.values = list(self.model.sample(time.monotonic() - self.start_time))
            self.values_publisher.publish(message)

    rclpy.init(args=args)
    node = None
    try:
        node = TactileDemoSource()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
