"""
hardware.launch.py — STM32 브릿지 노드 + URDF robot_state_publisher

실행:
  ros2 launch agv_bringup hardware.launch.py
  ros2 launch agv_bringup hardware.launch.py ws_url:=ws://192.168.1.10:8765
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, TimerAction
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

    gps_port = LaunchConfiguration('gps_port', default='/dev/gps')
    gps_baud = LaunchConfiguration('gps_baud', default='38400')

    # GPS 드라이버 (하드웨어 레이어 — EKF/navsat 없음, 다른 launch에서 추가)
    gps_config = Node(
        package='agv_bringup',
        executable='gps_config',
        name='gps_config',
        output='screen',
        parameters=[{'port': gps_port, 'baud': gps_baud}],
    )
    gps_driver = TimerAction(
        period=4.0,
        actions=[Node(
            package='agv_bringup',
            executable='gps_driver_node',
            name='gps_driver',
            output='screen',
            parameters=[{'port': gps_port, 'baud': gps_baud, 'frame_id': 'gps_link'}],
        )],
    )

    return LaunchDescription([
        DeclareLaunchArgument('ws_url',       default_value='ws://localhost:8765',
                              description='bridge.py WebSocket URL'),
        DeclareLaunchArgument('wheel_base',   default_value='0.60'),
        DeclareLaunchArgument('wheel_radius', default_value='0.15'),
        DeclareLaunchArgument('camera_x',     default_value='0.25'),
        DeclareLaunchArgument('camera_z',     default_value='0.30'),
        DeclareLaunchArgument('camera_pitch', default_value='0.0'),
        DeclareLaunchArgument('gps_port',     default_value='/dev/gps'),
        DeclareLaunchArgument('gps_baud',     default_value='38400'),

        description_launch,
        bridge_node,
        gps_config,
        gps_driver,
    ])
