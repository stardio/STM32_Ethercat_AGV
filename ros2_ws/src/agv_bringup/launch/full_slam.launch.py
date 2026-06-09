"""
full_slam.launch.py — 전체 스택 (하드웨어 + SLAM + Nav2)

맵핑 모드: AGV를 직접 조종하며 환경 맵을 생성한다.

실행:
  ros2 launch agv_bringup full_slam.launch.py
  ros2 launch agv_bringup full_slam.launch.py ws_url:=ws://192.168.1.10:8765
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg = FindPackageShare('agv_bringup')

    ws_url   = LaunchConfiguration('ws_url',   default='ws://localhost:8765')
    db_path  = LaunchConfiguration('db_path',  default='~/.ros/rtabmap.db')
    camera_x = LaunchConfiguration('camera_x', default='0.25')
    camera_z = LaunchConfiguration('camera_z', default='0.30')

    hardware = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'hardware.launch.py'])]),
        launch_arguments={'ws_url': ws_url, 'camera_x': camera_x, 'camera_z': camera_z}.items(),
    )
    slam = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'slam.launch.py'])]),
        launch_arguments={'db_path': db_path}.items(),
    )
    nav2 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([PathJoinSubstitution([pkg, 'launch', 'nav2.launch.py'])]),
    )

    return LaunchDescription([
        DeclareLaunchArgument('ws_url',   default_value='ws://localhost:8765'),
        DeclareLaunchArgument('db_path',  default_value='~/.ros/rtabmap.db'),
        DeclareLaunchArgument('camera_x', default_value='0.25'),
        DeclareLaunchArgument('camera_z', default_value='0.30'),

        hardware,
        slam,
        nav2,
    ])
