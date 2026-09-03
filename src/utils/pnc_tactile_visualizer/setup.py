from glob import glob
from setuptools import find_packages, setup

package_name = 'pnc_tactile_visualizer'
setup(
    name=package_name, version='0.1.0', packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml', 'README.md']),
        ('share/' + package_name + '/config', glob('config/*.json')),
    ],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='PNC Hand maintainers', maintainer_email='support@example.com',
    description='Frame-attached 47-zone PNC tactile heatmap for Foxglove.',
    license='Apache-2.0',
    entry_points={'console_scripts': [
        'tactile_visualizer = pnc_tactile_visualizer.node:main',
    ]},
)
