#!/usr/bin/env python3
"""Cross-check launch, YAML, and xacro contracts without touching hardware."""

import ast
import sys
from pathlib import Path
from xml.etree import ElementTree

import yaml


EXPECTED_HARDWARE_KEYS = {
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
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    package_root = Path(sys.argv[1]).resolve()
    launch_path = package_root / 'launch' / 'ssc_tactile_hand.launch.py'
    hardware_path = package_root / 'config' / 'tactile_hand.yaml'
    controller_path = package_root / 'config' / 'controller_manager.yaml'
    xacro_path = package_root / 'urdf' / 'ssc_tactile_hand.urdf.xacro'
    broadcaster_root = package_root.parents[1] / 'utils' / 'state_interfaces_broadcaster'

    launch_source = launch_path.read_text(encoding='utf-8')
    launch_tree = ast.parse(launch_source)
    hardware_keys = None
    for statement in launch_tree.body:
        if isinstance(statement, ast.Assign):
            if any(isinstance(target, ast.Name) and target.id == 'HARDWARE_KEYS'
                   for target in statement.targets):
                hardware_keys = set(ast.literal_eval(statement.value))
                break
    require(hardware_keys == EXPECTED_HARDWARE_KEYS, 'launch hardware-key contract changed')
    require(
        "controller_manager_path = f'/{node_namespace}/controller_manager'" in launch_source,
        'spawner controller-manager path is not namespace-derived',
    )
    require(
        launch_source.count('namespace=node_namespace') == 3,
        'robot state publisher, controller manager, and spawner must share the tactile namespace',
    )
    require(
        "package='robot_state_publisher'" in launch_source,
        'robot_state_publisher is required for the Jazzy robot_description topic',
    )
    require(
        "remappings=[('robot_description', robot_description_topic)]" in launch_source,
        'controller manager robot_description subscription is not namespace-safe',
    )
    require(
        "'--param-file',\n                controller_config" in launch_source,
        'broadcaster spawner must receive the controller parameter file',
    )
    compile(launch_source, str(launch_path), 'exec')

    hardware = yaml.safe_load(hardware_path.read_text(encoding='utf-8'))['hardware']
    require(set(hardware) == EXPECTED_HARDWARE_KEYS, 'hardware YAML keys differ from launch keys')
    require(hardware['spi_mode'] == 0, 'SPI mode must remain fixed at zero')
    require(hardware['response_check'] == 'strict', 'production response checking must be strict')
    require(len(hardware['cs_lines']) == 9, 'exactly nine ordered CS lines are required')
    require(len(set(hardware['cs_lines'])) == 9, 'CS lines must be unique')

    xacro_root = ElementTree.parse(xacro_path).getroot()
    xacro_namespace = '{http://www.ros.org/wiki/xacro}'
    xacro_args = {
        element.attrib['name']
        for element in xacro_root.findall(f'{xacro_namespace}arg')
    }
    hardware_element = xacro_root.find('./ros2_control/hardware')
    require(hardware_element is not None, 'xacro hardware block is missing')
    xacro_params = {
        element.attrib['name']
        for element in hardware_element.findall('param')
    }
    require(xacro_args == EXPECTED_HARDWARE_KEYS, 'xacro arguments differ from launch keys')
    require(xacro_params == EXPECTED_HARDWARE_KEYS, 'xacro hardware params differ from launch keys')

    sensors = xacro_root.findall('./ros2_control/sensor')
    require(len(sensors) == 54, 'xacro must export exactly 54 tactile sensors')
    expected_interfaces = []
    for chip in range(9):
        for channel in range(6):
            sensor_name = f'raa{chip}_ch{channel}'
            sensor = next((item for item in sensors if item.attrib['name'] == sensor_name), None)
            require(sensor is not None, f'missing sensor {sensor_name}')
            names = [item.attrib['name'] for item in sensor.findall('state_interface')]
            require(names == ['raw_i', 'raw_q', 'value'], f'bad state interfaces for {sensor_name}')
            expected_interfaces.extend(f'{sensor_name}/{name}' for name in names)

    controller = yaml.safe_load(controller_path.read_text(encoding='utf-8'))
    require('/**/controller_manager' in controller, 'controller manager YAML is not namespace-safe')
    broadcaster_key = '/**/tactile_hand_state_broadcaster'
    require(broadcaster_key in controller, 'broadcaster YAML is not namespace-safe')
    interfaces = controller[broadcaster_key]['ros__parameters']['interfaces']
    require(interfaces == expected_interfaces, 'broadcaster interface list differs from xacro')

    broadcaster_manifest = ElementTree.parse(broadcaster_root / 'package.xml').getroot()
    require(
        broadcaster_manifest.findtext('name') == 'state_interfaces_broadcaster',
        'state_interfaces_broadcaster source package is not vendored',
    )
    require(
        (broadcaster_root / 'UPSTREAM_PROVENANCE.md').is_file(),
        'state_interfaces_broadcaster upstream provenance is missing',
    )
    require(
        (broadcaster_root / 'LICENSE').is_file(),
        'state_interfaces_broadcaster Apache-2.0 license is missing',
    )

    bringup_manifest = ElementTree.parse(package_root / 'package.xml').getroot()
    runtime_dependencies = {
        element.text for element in bringup_manifest.findall('exec_depend')
    }
    require(
        'robot_state_publisher' in runtime_dependencies,
        'bringup must declare robot_state_publisher as a runtime dependency',
    )

    print('bringup configuration checks passed')


if __name__ == '__main__':
    main()
