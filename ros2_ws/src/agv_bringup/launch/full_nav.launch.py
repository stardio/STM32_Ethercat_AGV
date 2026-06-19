"""
full_nav.launch.py — 전체 스택 (하드웨어 + 위치추정 + Nav2)

자율주행 모드: 저장된 맵으로 위치추정 + Nav2 goal 수행.

실행:
  ros2 launch agv_bringup full_nav.launch.py db_path:=~/.ros/rtabmap.db
"""
import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
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

    # GPS EKF: hardware의 gps_driver(t=4s)가 올라온 후 시작 (t=5s)
    # /odom → /odometry/filtered (navsat_transform 입력용)
    ekf_gps = TimerAction(
        period=5.0,
        actions=[Node(
            package='robot_localization',
            executable='ekf_node',
            name='ekf_gps_node',
            output='screen',
            parameters=[PathJoinSubstitution([pkg, 'config', 'ekf_gps.yaml'])],
            remappings=[('odometry/filtered', '/odometry/filtered')],
        )],
    )

    # navsat_transform: EKF(t=5s) 초기화 후 시작 (t=9s)
    # /gps/fix + /odometry/filtered → /odometry/gps (ENU)
    navsat = TimerAction(
        period=9.0,
        actions=[Node(
            package='robot_localization',
            executable='navsat_transform_node',
            name='navsat_transform',
            output='screen',
            parameters=[PathJoinSubstitution([pkg, 'config', 'navsat_transform.yaml'])],
            remappings=[
                ('/imu/data',          '/camera/imu'),
                ('/odometry/filtered', '/odometry/filtered'),
                ('/odometry/gps',      '/odometry/gps'),
                ('/gps/filtered',      '/gps/filtered'),
            ],
        )],
    )

    return LaunchDescription([
        DeclareLaunchArgument('ws_url',  default_value='ws://localhost:8765'),
        DeclareLaunchArgument('db_path', default_value='~/.ros/rtabmap.db',
                              description='Path to existing RTAB-Map database'),

        hardware,
        localization,
        nav2,
        ekf_gps,
        navsat,
    ])
