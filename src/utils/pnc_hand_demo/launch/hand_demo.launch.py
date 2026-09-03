"""One isolated ROS domain: mock motion, simulated tactile, 3D markers, bridge."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument, GroupAction, IncludeLaunchDescription, LogInfo,
    OpaqueFunction, SetEnvironmentVariable,
)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def launch_setup(context):
    domain = LaunchConfiguration('demo_domain_id').perform(context)
    hand_side = LaunchConfiguration('hand_side').perform(context)
    scenario = LaunchConfiguration('scenario').perform(context)
    port = LaunchConfiguration('foxglove_port').perform(context)
    if not domain.isdecimal() or not 1 <= int(domain) <= 232:
        raise ValueError('demo_domain_id must be in 1..232; choose an unused ROS domain')
    if hand_side not in ('left', 'right'):
        raise ValueError('hand_side must be left or right')
    if scenario not in ('sweep', 'manual'):
        raise ValueError('scenario must be sweep or manual')
    if not port.isdecimal() or not 1 <= int(port) <= 65535:
        raise ValueError('foxglove_port must be in 1..65535')

    motion_launch = os.path.join(
        get_package_share_directory('inspire_rh56e2_hand_bringup'), 'launch',
        'inspire_rh56e2_hand_joint_position_control.launch.py',
    )
    process_env = {'ROS_DOMAIN_ID': domain}
    return [GroupAction(scoped=True, actions=[
        # The launch context environment is inherited by every included motion
        # process (controller manager, RSP, spawners and gripper adapter).
        SetEnvironmentVariable('ROS_DOMAIN_ID', domain),
        LogInfo(msg=f'SIMULATION: ROS_DOMAIN_ID={domain}; choose a domain unused by hardware.'),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(motion_launch),
            launch_arguments={
                'use_mock_hardware': 'true', 'hand_side': hand_side,
                'launch_foxglove': 'false', 'motion_mode': '',
            }.items(),
        ),
        Node(
            package='pnc_hand_demo', executable='tactile_demo_source',
            output='screen', additional_env=process_env,
            parameters=[{'scenario': scenario, 'publish_rate': 40.0}],
        ),
        Node(
            package='pnc_tactile_visualizer', executable='tactile_visualizer',
            output='screen', additional_env=process_env,
            parameters=[{
                'hand_side': hand_side, 'mapping_profile': 'demo',
                'color_min': 0.0, 'color_max': 1.0,
            }],
        ),
        Node(
            package='foxglove_bridge', executable='foxglove_bridge',
            name='foxglove_bridge', output='screen', additional_env=process_env,
            parameters=[{
                'port': int(port), 'address': '0.0.0.0',
                'capabilities': [
                    'clientPublish', 'parameters', 'parametersSubscribe',
                    'services', 'connectionGraph', 'assets',
                ],
                # URDF meshes use uppercase .STL; support either case explicitly.
                'asset_uri_allowlist': [
                    '^package://inspire_rh56e2_hand_description/meshes/'
                    '(left|right)/[A-Za-z0-9_]+[.][sS][tT][lL]$',
                ],
            }],
        ),
    ])]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('demo_domain_id', default_value='77',
                              description='Unused ROS domain for every demo process (1..232)'),
        DeclareLaunchArgument('hand_side', default_value='left', choices=['left', 'right']),
        DeclareLaunchArgument('scenario', default_value='sweep', choices=['sweep', 'manual']),
        DeclareLaunchArgument('foxglove_port', default_value='8765'),
        OpaqueFunction(function=launch_setup),
    ])
