"""Publish frame-locked surface polygons with independent per-zone colors."""
import json
import math
from pathlib import Path
import time

from ament_index_python.packages import get_package_share_directory
from control_msgs.msg import Float64Values, Keys
from geometry_msgs.msg import Point
import rclpy
from rclpy.clock import Clock, ClockType
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import String
from visualization_msgs.msg import Marker, MarkerArray

from .core import NamedFrame, load_zones, response_color


class TactileVisualizer(Node):
    def __init__(self):
        super().__init__('pnc_tactile_visualizer')
        defaults = {
            'hand_side': 'left', 'mapping_file': '', 'mapping_profile': 'unmapped',
            'names_topic': '/tactile/tactile_hand_state_broadcaster/names',
            'values_topic': '/tactile/tactile_hand_state_broadcaster/values',
            'markers_topic': '/tactile/markers',
            'status_topic': '/tactile/visualization_status',
            'frame_prefix': '', 'color_min': 0.0, 'color_max': 1.0,
            'timeout_sec': 0.5, 'publish_rate': 20.0,
        }
        for name, value in defaults.items():
            self.declare_parameter(name, value)
        parameter = lambda key: self.get_parameter(key).value
        side = parameter('hand_side')
        if side not in ('left', 'right'):
            raise ValueError('hand_side must be left or right')
        self.profile = parameter('mapping_profile')
        path = parameter('mapping_file') or str(
            Path(get_package_share_directory('pnc_tactile_visualizer')) /
            'config' / f'pnc_zones_{side}.json')
        self.zones = load_zones(json.loads(Path(path).read_text()), side, self.profile)
        self.minimum, self.maximum = parameter('color_min'), parameter('color_max')
        response_color(0.0, self.minimum, self.maximum)
        self.timeout, rate = parameter('timeout_sec'), parameter('publish_rate')
        if not math.isfinite(self.timeout) or self.timeout <= 0 or not math.isfinite(rate) or rate <= 0:
            raise ValueError('timeout_sec and publish_rate must be positive finite numbers')
        self.prefix = parameter('frame_prefix')
        self.frame = NamedFrame()
        latched = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                             durability=DurabilityPolicy.TRANSIENT_LOCAL)
        stream = QoSProfile(depth=5, reliability=ReliabilityPolicy.BEST_EFFORT)
        self.create_subscription(Keys, parameter('names_topic'), self.on_names, latched)
        self.create_subscription(Float64Values, parameter('values_topic'), self.on_values, stream)
        self.publisher = self.create_publisher(MarkerArray, parameter('markers_topic'), latched)
        self.status_publisher = self.create_publisher(String, parameter('status_topic'), latched)
        # A steady timer grays stale data even when a simulation's ROS clock stops.
        self.create_timer(1.0 / rate, self.publish_frame, clock=Clock(clock_type=ClockType.STEADY_TIME))
        self.last_status = None
        self.get_logger().info(f'47-zone {side} heatmap; mapping={self.profile}; relative response only')

    def on_names(self, message):
        self.frame.set_names(message.keys)

    def on_values(self, message):
        self.frame.set_values(message.values, time.monotonic())

    def publish_frame(self):
        now, markers, known = time.monotonic(), [], 0
        stamp = self.get_clock().now().to_msg()
        for index, zone in enumerate(self.zones):
            value = self.frame.value(zone.channel, now, self.timeout)
            if math.isfinite(value):
                value = value * zone.gain + zone.offset
                known += 1
            marker = Marker()
            # Frame locking keeps the patch attached while fingers move.
            marker.header.stamp = stamp
            marker.header.frame_id = self.prefix + zone.frame_id
            marker.ns, marker.id = 'pnc/' + zone.id, index
            marker.type, marker.action = Marker.TRIANGLE_LIST, Marker.ADD
            marker.pose.orientation.w = 1.0
            marker.scale.x = marker.scale.y = marker.scale.z = 1.0
            marker.frame_locked = True
            marker.color.r, marker.color.g, marker.color.b, marker.color.a = response_color(
                value, self.minimum, self.maximum)
            marker.points = [Point(x=x, y=y, z=z) for x, y, z in zone.triangles]
            # If this node itself disappears, do not leave a live-looking heatmap.
            marker.lifetime.sec, marker.lifetime.nanosec = 1, 0
            markers.append(marker)
        self.publisher.publish(MarkerArray(markers=markers))
        status = {
            'stream': self.frame.state(now, self.timeout), 'mapping': self.profile,
            'known_zones': known, 'total_zones': 47, 'units': 'relative_response',
            'color_min': self.minimum, 'color_max': self.maximum,
            'device_health': 'simulated' if self.profile == 'demo' else 'not_provided_by_hardware',
        }
        serialized = json.dumps(status, sort_keys=True)
        if serialized != self.last_status:
            self.status_publisher.publish(String(data=serialized))
            self.last_status = serialized


def main(args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = TactileVisualizer()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
