from glob import glob
from setuptools import find_packages, setup


setup(
    name='pnc_hand_demo',
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/pnc_hand_demo']),
        ('share/pnc_hand_demo', ['package.xml', 'README.md']),
        ('share/pnc_hand_demo/launch', glob('launch/*.launch.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='PNC hand maintainers',
    maintainer_email='maintainer@example.com',
    description='Isolated mock hand and 54-channel tactile demonstration',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'tactile_demo_source = pnc_hand_demo.source:main',
        ],
    },
)
