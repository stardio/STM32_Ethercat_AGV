from setuptools import setup
import os
from glob import glob

package_name = 'agv_bringup'

setup(
    name=package_name,
    version='2.0.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'),  glob('launch/*.py')),
        (os.path.join('share', package_name, 'config'),  glob('config/*.yaml')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    entry_points={
        'console_scripts': [
            'stm32_bridge_node = agv_bringup.stm32_bridge_node:main',
        ],
    },
)
