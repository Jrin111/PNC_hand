#!/usr/bin/env python3
"""
Real-hardware scenario: pre-position fingers in mode 0, then switch to Force Control to hold.

This avoids the Force Control "dead-band" problem (in mode 1 a low force setpoint may not move
the fingers from open): instead we first drive the fingers ONTO the object with reliable
position control (mode 0), and only THEN switch the gripping fingers to mode 1 so they maintain
a gentle force. One joint (thumb_yaw) is deliberately kept in mode 0 the whole time so it holds
its posed angle and does not get force-driven.

SCENARIO (defaults match the requested test)
  Phase 0 - wait for hardware + controllers + force feedback
  Phase 1 - set force = 200 g (all) + mode 0 (all) + OPEN the whole hand
  Phase 2 - mode 0 + drive to the hold pose (raw angles below); wait to settle
              thumb_yaw=66, thumb_pitch=820, index=610, middle=612, ring/pinky=open
  Phase 3 - at the pose, in this order: (a) switch ONLY the grip fingers
              (thumb_pitch, index, middle) to mode 1, (b) set their grip force; the fingers stay
              at the pose and Force Control maintains the grip there (thumb_yaw stays in mode 0)
  Phase 4 - hold/wait for 8 s, logging the grip force each second
  Phase 5 - back to mode 0 and OPEN the whole hand

Angles are given as RAW hardware units (0-1000, where 0 = fully closed and 1000 = fully open on
this driver); the script converts them to radians. Joint order everywhere:
  thumb_yaw, thumb_pitch, index, middle, ring, pinky

PREREQUISITE: hand connected and launched WITHOUT mock hardware:
  ros2 launch inspire_rh56e2_hand_bringup inspire_rh56e2_hand_joint_position_control.launch.py

USAGE
  python3 test_pose_then_force_hold.py
  python3 test_pose_then_force_hold.py --pose-raw 66,820,610,612,1000,1000 \
      --grip thumb_pitch,index,middle --force 100 --hold 8

SAFETY: moves a real hand. On exit (even Ctrl-C) it restores mode 0 and opens the hand.
"""

import argparse
import sys
import time

import rclpy
from control_msgs.msg import DynamicJointState
from rclpy.node import Node
from rclpy.qos import QoSDurabilityPolicy, QoSHistoryPolicy, QoSProfile, QoSReliabilityPolicy
from sensor_msgs.msg import JointState
from std_msgs.msg import Float64MultiArray

# Short finger names (for --grip and indexing) and their full joint names (for state lookup).
FINGERS = ['thumb_yaw', 'thumb_pitch', 'index', 'middle', 'ring', 'pinky']
JOINT_ORDER = [
    'thumb_proximal_yaw_joint',
    'thumb_proximal_pitch_joint',
    'index_proximal_joint',
    'middle_proximal_joint',
    'ring_proximal_joint',
    'pinky_proximal_joint',
]
# Upper joint limits (rad), from the ros2_control xacro. Used for raw(0-1000) -> rad conversion.
JOINT_MAX = [1.658, 0.62, 1.4381, 1.4381, 1.4381, 1.4381]

POSITION_TOPIC = '/inspire_rh56e2_hand_joint_position_controller/commands'
FORCE_TOPIC = '/inspire_rh56e2_hand_force_threshold_controller/commands'
MODE_TOPIC = '/inspire_rh56e2_hand_motion_mode_controller/commands'
JOINT_STATES_TOPIC = '/joint_states_hw'
DYNAMIC_STATES_TOPIC = '/dynamic_joint_states'

OPEN_TARGET = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
# Initial force (g) set on ALL fingers in Phase 1, and the hold force kept on the non-grip
# fingers so they reach/hold their posed angle in mode 0.
HOLD_OTHERS_FORCE = 200

# RELIABLE + TRANSIENT_LOCAL: compatible with the VOLATILE controller subs and any
# TRANSIENT_LOCAL relay (e.g. foxglove_bridge).
COMMAND_QOS = QoSProfile(
    history=QoSHistoryPolicy.KEEP_LAST,
    depth=1,
    reliability=QoSReliabilityPolicy.RELIABLE,
    durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
)


def raw_to_rad(hw, i):
    """Convert a raw hardware angle (0-1000) to radians for joint i (driver convention:
    rad = (1 - hw/1000) * joint_max, so hw=1000 -> open(0 rad), hw=0 -> fully closed(max))."""
    hw = max(0.0, min(1000.0, float(hw)))
    return (1.0 - hw / 1000.0) * JOINT_MAX[i]


class PoseThenForceHold(Node):
    def __init__(self, args):
        super().__init__('pose_then_force_hold')
        self.args = args
        self.pos_pub = self.create_publisher(Float64MultiArray, POSITION_TOPIC, COMMAND_QOS)
        self.force_pub = self.create_publisher(Float64MultiArray, FORCE_TOPIC, COMMAND_QOS)
        self.mode_pub = self.create_publisher(Float64MultiArray, MODE_TOPIC, COMMAND_QOS)
        self.create_subscription(JointState, JOINT_STATES_TOPIC, self._on_js, 10)
        self.create_subscription(DynamicJointState, DYNAMIC_STATES_TOPIC, self._on_djs, 10)
        self.positions = {}  # joint name -> rad
        self.forces = {}     # joint name -> grams
        self.failures = []
        # Per-finger pose target in radians and the set of grip-finger indices.
        self.pose_rad = [raw_to_rad(hw, i) for i, hw in enumerate(args.pose_raw)]
        self.grip_idx = args.grip_idx

    def _on_js(self, msg):
        for name, pos in zip(msg.name, msg.position):
            self.positions[name] = pos

    def _on_djs(self, msg):
        for name, iv in zip(msg.joint_names, msg.interface_values):
            names = list(iv.interface_names)
            if 'force' in names:
                self.forces[name] = iv.values[names.index('force')]

    def _publish(self, pub, values):
        msg = Float64MultiArray()
        msg.data = [float(v) for v in values]
        pub.publish(msg)

    def spin_for(self, seconds, publish_position=None):
        end = time.time() + seconds
        while rclpy.ok() and time.time() < end:
            if publish_position is not None:
                self._publish(self.pos_pub, publish_position)
            rclpy.spin_once(self, timeout_sec=0.05)

    def wait_ready(self, timeout=30.0):
        self.get_logger().info('Phase 0: waiting for hardware, controllers and force feedback...')
        end = time.time() + timeout
        while rclpy.ok() and time.time() < end:
            rclpy.spin_once(self, timeout_sec=0.1)
            have = (all(j in self.positions for j in JOINT_ORDER)
                    and all(j in self.forces for j in JOINT_ORDER)
                    and self.pos_pub.get_subscription_count() > 0
                    and self.force_pub.get_subscription_count() > 0
                    and self.mode_pub.get_subscription_count() > 0)
            if have:
                self.get_logger().info('  ready: position + force feedback present; controllers up')
                return True
        self.get_logger().error('  NOT ready (timeout)')
        return False

    def _report(self, target, label):
        for name, want in zip(JOINT_ORDER, target):
            self.get_logger().info(
                '  [%s] %-26s cmd=%.3f act=%.3f force=%7.1f g'
                % (label, name, want, self.positions.get(name, float('nan')),
                   self.forces.get(name, float('nan'))))

    def _grip_forces(self):
        return {FINGERS[i]: self.forces.get(JOINT_ORDER[i], float('nan')) for i in self.grip_idx}

    def run(self):
        if not self.wait_ready():
            self.failures.append('hardware / controllers / force feedback not ready')
            return False

        grip_names = [FINGERS[i] for i in self.grip_idx]
        self.get_logger().info(
            'Scenario: pose=%s (raw=%s), grip=%s, force=%dg, hold=%ds'
            % ([round(x, 3) for x in self.pose_rad], self.args.pose_raw, grip_names,
               self.args.force, self.args.hold))

        # Phase 1: set force=200 g (all) + mode 0 (all) + OPEN the whole hand.
        self.get_logger().info('Phase 1: force=%dg (all) + mode 0 (all) + OPEN full hand'
                               % HOLD_OTHERS_FORCE)
        self._publish(self.force_pub, [HOLD_OTHERS_FORCE] * 6)
        self._publish(self.mode_pub, [0] * 6)
        self.spin_for(2.0, publish_position=OPEN_TARGET)

        # Phase 2: still mode 0, drive to the hold pose, let it settle.
        self.get_logger().info('Phase 2: mode 0, move to pose %s'
                               % [round(x, 3) for x in self.pose_rad])
        self._publish(self.mode_pub, [0] * 6)
        self.spin_for(self.args.settle, publish_position=self.pose_rad)
        self._report(self.pose_rad, 'POSE')

        # Phase 3: engage the grip in the requested order -> (a) mode 1, (b) force. The grip
        # fingers are left at the hold pose; Force Control then maintains the grip at that pose
        # up to the FORCE_SET setpoint (no extra "close past contact" command).
        mode_cmd = [0] * 6
        for i in self.grip_idx:
            mode_cmd[i] = 1
        self.get_logger().info('Phase 3a: set mode=%s (thumb_yaw stays mode 0)' % mode_cmd)
        self._publish(self.mode_pub, mode_cmd)
        self.spin_for(0.3, publish_position=self.pose_rad)

        force_cmd = [HOLD_OTHERS_FORCE] * 6
        for i in self.grip_idx:
            force_cmd[i] = self.args.force
        self.get_logger().info('Phase 3b: set force=%s' % force_cmd)
        self._publish(self.force_pub, force_cmd)
        self.spin_for(0.3, publish_position=self.pose_rad)

        # Phase 4: hold/wait at the pose while Force Control maintains the grip; the mode-0
        # joints keep their pose too.
        self.get_logger().info('Phase 4: HOLD/wait %ds in force control' % self.args.hold)
        t_end = time.time() + self.args.hold
        while rclpy.ok() and time.time() < t_end:
            self.spin_for(1.0, publish_position=self.pose_rad)
            gf = self._grip_forces()
            self.get_logger().info('  hold t=%2ds  grip force: %s'
                                   % (round(t_end - time.time()),
                                      {k: round(v, 1) for k, v in gf.items()}))
        self._report(self.pose_rad, 'HOLD-END')
        gf = self._grip_forces()
        weak = [k for k, v in gf.items() if abs(v) < self.args.contact]
        if weak:
            self.failures.append('grip fingers below contact force (%dg): %s'
                                 % (self.args.contact, weak))

        # Phase 5: release -> mode 0 everywhere, open the whole hand.
        self.get_logger().info('Phase 5: mode 0 + OPEN full hand')
        self._publish(self.mode_pub, [0] * 6)
        self.spin_for(0.5)
        self.spin_for(3.0, publish_position=OPEN_TARGET)
        self._report(OPEN_TARGET, 'OPEN')
        still = [JOINT_ORDER[i] for i in self.grip_idx
                 if abs(self.forces.get(JOINT_ORDER[i], 0.0)) >= self.args.contact]
        if still:
            self.failures.append('grip force not released after OPEN on: %s' % still)

        return len(self.failures) == 0

    def safe_release(self):
        """Best-effort cleanup: mode 0 then OPEN, so the hand never stays clamped."""
        try:
            self.get_logger().info('Cleanup: motion_mode = [0]*6 then OPEN')
            self._publish(self.mode_pub, [0] * 6)
            self.spin_for(0.5)
            self._publish(self.pos_pub, OPEN_TARGET)
            self.spin_for(2.0, publish_position=OPEN_TARGET)
        except Exception:
            pass

    def print_summary(self, ok):
        self.get_logger().info('=' * 60)
        if ok:
            self.get_logger().info('POSE-THEN-FORCE-HOLD RESULT: PASS')
        else:
            self.get_logger().error('POSE-THEN-FORCE-HOLD RESULT: FAIL')
            for f in self.failures:
                self.get_logger().error('  - %s' % f)
        self.get_logger().info('=' * 60)


def _parse_pose_raw(value):
    parts = [p.strip() for p in str(value).split(',') if p.strip() != '']
    if len(parts) != 6:
        raise SystemExit('error: --pose-raw expects 6 comma-separated values, got %d' % len(parts))
    try:
        return [float(p) for p in parts]
    except ValueError:
        raise SystemExit('error: --pose-raw has a non-numeric value: %r' % value)


def _parse_grip(value):
    idx = []
    for name in [p.strip() for p in str(value).split(',') if p.strip() != '']:
        if name not in FINGERS:
            raise SystemExit('error: --grip unknown finger %r (valid: %s)'
                             % (name, ', '.join(FINGERS)))
        idx.append(FINGERS.index(name))
    if not idx:
        raise SystemExit('error: --grip needs at least one finger')
    return idx


def parse_args():
    parser = argparse.ArgumentParser(
        description='Pre-position in mode 0, then Force Control hold (real hardware)')
    parser.add_argument('--pose-raw', default='66,820,610,612,1000,1000',
                        help='6 raw angles (0-1000) for thumb_yaw,thumb_pitch,index,middle,ring,'
                             'pinky. 1000 = open, 0 = fully closed. Default is the requested pose.')
    parser.add_argument('--grip', default='thumb_pitch,index,middle',
                        help='fingers switched to Force Control (mode 1) at the target. '
                             'Default: thumb_pitch,index,middle (thumb_yaw deliberately excluded).')
    parser.add_argument('--force', type=int, default=100,
                        help='grip force (g) for the --grip fingers in mode 1 (default 100)')
    parser.add_argument('--hold', type=int, default=8, help='hold time in seconds (default 8)')
    parser.add_argument('--settle', type=float, default=2.0,
                        help='seconds to wait for the pose to settle in mode 0 (default 2.0)')
    parser.add_argument('--contact', type=int, default=50,
                        help='contact-detection force in grams for PASS/FAIL (default 50)')
    args, _ = parser.parse_known_args()
    args.pose_raw = _parse_pose_raw(args.pose_raw)
    args.grip_idx = _parse_grip(args.grip)
    return args


def main():
    args = parse_args()
    rclpy.init()
    node = PoseThenForceHold(args)
    ok = False
    try:
        ok = node.run()
    except KeyboardInterrupt:
        node.get_logger().warn('interrupted - releasing hand')
    finally:
        node.safe_release()
        node.print_summary(ok)
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
