#!/usr/bin/env python3
"""Launch the RZ/V2H-direct nine-device PNC tactile hand."""

import os
from typing import Dict, List

import xacro
import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchContext, LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


HARDWARE_KEYS = (
    'spi_device',
    'spi_speed_hz',
    'spi_mode',
    'bits_per_word',
    'gpio_chip',
    'cs_lines',
    'active_devices',
    'channels_per_device',
    'auto_tare',
    'ema_shift',
    'measurement_wait_us',
    'response_check',
    'max_consecutive_failures',
    'recovery_interval_frames',
)

ARGUMENT_DESCRIPTIONS = {
    'spi_device': 'Linux spidev device for the shared SPI6 bus.',
    'spi_speed_hz': 'SPI clock frequency in Hz.',
    'spi_mode': 'SPI mode; the RAA2S4704 configuration uses mode 0.',
    'bits_per_word': 'SPI bits per word; fixed at 8.',
    'gpio_chip': 'GPIO character device containing the nine CS lines.',
    'cs_lines': 'Ordered comma-separated GPIO offsets for raa0 through raa8.',
    'active_devices': 'Number of active RAA devices, from 1 through 9.',
    'channels_per_device': 'Channels exported per RAA; fixed at 6.',
    'auto_tare': 'Run automatic tare during hardware activation.',
    'ema_shift': 'EMA shift applied to acquired channel values.',
    'measurement_wait_us': 'Wait after starting each channel measurement, in microseconds.',
    'response_check': 'RAA response validation mode: strict or log-only.',
    'max_consecutive_failures': 'Consecutive failures before one RAA is isolated.',
    'recovery_interval_frames': 'Frames between attempts to recover an isolated RAA.',
    'update_rate': 'Controller manager update rate in Hz.',
    'namespace': 'ROS namespace used to isolate the tactile controller manager.',
}


def _load_hardware_defaults(package_share: str) -> Dict[str, object]:
    config_path = os.path.join(package_share, 'config', 'tactile_hand.yaml')
    with open(config_path, 'r', encoding='utf-8') as config_file:
        data = yaml.safe_load(config_file) or {}

    hardware = data.get('hardware')
    if not isinstance(hardware, dict):
        raise RuntimeError(f'{config_path} must contain a hardware mapping')

    missing = sorted(set(HARDWARE_KEYS) - set(hardware))
    if missing:
        raise RuntimeError(f'{config_path} is missing hardware keys: {", ".join(missing)}')

    cs_lines = hardware['cs_lines']
    if not isinstance(cs_lines, list) or len(cs_lines) != 9:
        raise RuntimeError(f'{config_path}: cs_lines must be an ordered list of 9 offsets')

    return hardware


def _as_launch_default(value: object) -> str:
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, list):
        return ','.join(str(item) for item in value)
    return str(value)


def _launch_setup(context: LaunchContext) -> List[Node]:
    package_share = get_package_share_directory('ssc_tactile_hand_bringup')
    resolved = {
        key: LaunchConfiguration(key).perform(context).strip()
        for key in HARDWARE_KEYS
    }

    try:
        active_devices = int(resolved['active_devices'])
        channels_per_device = int(resolved['channels_per_device'])
        bits_per_word = int(resolved['bits_per_word'])
        spi_mode = int(resolved['spi_mode'])
        max_consecutive_failures = int(resolved['max_consecutive_failures'])
        recovery_interval_frames = int(resolved['recovery_interval_frames'])
        update_rate = int(LaunchConfiguration('update_rate').perform(context).strip())
    except ValueError as error:
        raise RuntimeError('numeric tactile-hand launch arguments must be integers') from error

    cs_lines = [line.strip() for line in resolved['cs_lines'].split(',') if line.strip()]
    if not 1 <= active_devices <= 9:
        raise RuntimeError('active_devices must be between 1 and 9')
    if len(cs_lines) != 9:
        raise RuntimeError('cs_lines must retain the complete ordered list of 9 offsets')
    if channels_per_device != 6:
        raise RuntimeError('channels_per_device is fixed at 6')
    if bits_per_word != 8:
        raise RuntimeError('bits_per_word is fixed at 8')
    if spi_mode != 0:
        raise RuntimeError('spi_mode is fixed at 0')
    if max_consecutive_failures <= 0:
        raise RuntimeError('max_consecutive_failures must be greater than zero')
    if recovery_interval_frames <= 0:
        raise RuntimeError('recovery_interval_frames must be greater than zero')
    if update_rate <= 0:
        raise RuntimeError('update_rate must be greater than zero')
    if resolved['auto_tare'].lower() not in ('true', 'false'):
        raise RuntimeError('auto_tare must be true or false')
    if resolved['response_check'] not in ('strict', 'log-only'):
        raise RuntimeError("response_check must be 'strict' or 'log-only'")

    node_namespace = LaunchConfiguration('namespace').perform(context).strip().strip('/')
    if not node_namespace or any(character.isspace() for character in node_namespace):
        raise RuntimeError('namespace must be a non-empty ROS namespace without whitespace')
    controller_manager_path = f'/{node_namespace}/controller_manager'

    resolved['cs_lines'] = ','.join(cs_lines)
    xacro_path = os.path.join(
        package_share, 'urdf', 'ssc_tactile_hand.urdf.xacro'
    )
    robot_description_xml = xacro.process_file(
        xacro_path, mappings=resolved
    ).toxml()
    robot_description = {'robot_description': robot_description_xml}
    controller_config = os.path.join(
        package_share, 'config', 'controller_manager.yaml'
    )

    return [
        Node(
            package='controller_manager',
            executable='ros2_control_node',
            name='controller_manager',
            namespace=node_namespace,
            output='screen',
            parameters=[
                robot_description,
                controller_config,
                {'update_rate': update_rate},
            ],
        ),
        Node(
            package='controller_manager',
            executable='spawner',
            name='tactile_hand_state_broadcaster_spawner',
            namespace=node_namespace,
            output='screen',
            arguments=[
                'tactile_hand_state_broadcaster',
                '--controller-manager',
                controller_manager_path,
            ],
        ),
    ]


def generate_launch_description() -> LaunchDescription:
    package_share = get_package_share_directory('ssc_tactile_hand_bringup')
    defaults = _load_hardware_defaults(package_share)

    launch_arguments = [
        DeclareLaunchArgument(
            key,
            default_value=_as_launch_default(defaults[key]),
            description=ARGUMENT_DESCRIPTIONS[key],
        )
        for key in HARDWARE_KEYS
    ]
    launch_arguments.append(
        DeclareLaunchArgument(
            'update_rate',
            default_value='40',
            description=ARGUMENT_DESCRIPTIONS['update_rate'],
        )
    )
    launch_arguments.append(
        DeclareLaunchArgument(
            'namespace',
            default_value='tactile',
            description=ARGUMENT_DESCRIPTIONS['namespace'],
        )
    )

    return LaunchDescription([
        *launch_arguments,
        OpaqueFunction(function=_launch_setup),
    ])
