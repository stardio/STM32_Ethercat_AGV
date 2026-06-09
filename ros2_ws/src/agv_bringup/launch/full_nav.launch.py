"""
full_nav.launch.py — 전체 스택 (하드웨어 + 위치추정 + Nav2)

자율주행 모드: 저장된 맵으로 위치추정 + Nav2 goal 수행.

실행:
  ros2 launch agv_bringup full_nav.launch.py db_path:=~/.ros/rtabmap.db
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg = FindPackageShare('agv_bringup')

    ws_url  = LaunchConfiguration('ws_url',  default='ws://localhost:8765')
    db_path = LaunchConfiguration('db_path', default='~/.ros/rtabmap.db')

    hardware = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'hardware.launch.py'])]),
        launch_arguments={'ws_url': ws_url}.items(),
    )
    localization = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'localization.launch.py'])]),
        launch_arguments={'db_path': db_path}.items(),
    )
    nav2 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'nav2.launch.py'])]),
    )

    return LaunchDescription([
        DeclareLaunchArgument('ws_url',  default_value='ws://localhost:8765'),
        DeclareLaunchArgument('db_path', default_value='~/.ros/rtabmap.db',
                              description='Path to existing RTAB-Map database'),

        hardware,
        localization,
        nav2,
    ])
