"""
nav2.launch.py — Nav2 자율주행 스택

실행 (RTAB-Map이 /map 발행 중이어야 함):
  ros2 launch agv_bringup nav2.launch.py
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    nav2_params = PathJoinSubstitution([
        FindPackageShare('agv_bringup'), 'config', 'nav2_params.yaml'
    ])

    nav2_bringup_dir = FindPackageShare('nav2_bringup')

    nav2_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([nav2_bringup_dir, 'launch', 'navigation_launch.py'])
        ]),
        launch_arguments={
            'params_file': nav2_params,
            'use_sim_time': 'false',
            'autostart':    'true',
        }.items(),
    )

    return LaunchDescription([
        nav2_launch,
    ])
