"""
hardware.launch.py — STM32 브릿지 노드 + URDF robot_state_publisher

실행:
  ros2 launch agv_bringup hardware.launch.py
  ros2 launch agv_bringup hardware.launch.py ws_url:=ws://192.168.1.10:8765
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    ws_url        = LaunchConfiguration('ws_url',        default='ws://localhost:8765')
    wheel_base    = LaunchConfiguration('wheel_base',    default='0.60')
    wheel_radius  = LaunchConfiguration('wheel_radius',  default='0.15')
    camera_x      = LaunchConfiguration('camera_x',      default='0.25')
    camera_z      = LaunchConfiguration('camera_z',      default='0.30')
    camera_pitch  = LaunchConfiguration('camera_pitch',  default='0.0')

    description_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([FindPackageShare('agv_description'), 'launch', 'description.launch.py'])
        ]),
        launch_arguments={
            'camera_x':     camera_x,
            'camera_z':     camera_z,
            'camera_pitch': camera_pitch,
        }.items(),
    )

    bridge_node = Node(
        package='agv_bringup',
        executable='stm32_bridge_node',
        name='stm32_bridge_node',
        parameters=[{
            'ws_url':       ws_url,
            'wheel_base':   wheel_base,
            'wheel_radius': wheel_radius,
        }],
        output='screen',
    )

    return LaunchDescription([
        DeclareLaunchArgument('ws_url',       default_value='ws://localhost:8765',
                              description='bridge.py WebSocket URL'),
        DeclareLaunchArgument('wheel_base',   default_value='0.60'),
        DeclareLaunchArgument('wheel_radius', default_value='0.15'),
        DeclareLaunchArgument('camera_x',     default_value='0.25'),
        DeclareLaunchArgument('camera_z',     default_value='0.30'),
        DeclareLaunchArgument('camera_pitch', default_value='0.0'),

        description_launch,
        bridge_node,
    ])
