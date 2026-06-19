"""
gps.launch.py — GPS 단독 테스트용 (hardware.launch.py 없이 실행)

주의: hardware.launch.py / full_nav.launch.py / row_follow.launch.py 실행 중에는
      이 파일을 별도로 실행하지 않아도 됩니다.
      GPS 드라이버는 hardware.launch.py에 통합되어 자동 시작됩니다.

이 파일은 GPS 기능만 단독으로 테스트할 때 사용합니다:
  ros2 launch agv_bringup gps.launch.py
  ros2 launch agv_bringup gps.launch.py gps_port:=/dev/gps gps_baud:=38400

타임라인:
  t=0s   gps_config        : U-blox UBX 비활성화, 10Hz 설정
  t=4s   gps_driver        : /gps/fix 발행
  t=4s   ekf_gps_node      : /odom → /odometry/filtered
  t=9s   navsat_transform  : /gps/fix + /odometry/filtered → /odometry/gps (ENU)

토픽:
  /gps/fix            NavSatFix  — 원시 GPS 위치 (10Hz)
  /odometry/filtered  Odometry   — EKF 출력
  /odometry/gps       Odometry   — ENU 로컬 좌표
"""

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    pkg = get_package_share_directory('agv_bringup')
    navsat_cfg  = os.path.join(pkg, 'config', 'navsat_transform.yaml')
    ekf_gps_cfg = os.path.join(pkg, 'config', 'ekf_gps.yaml')

    port_arg = DeclareLaunchArgument('gps_port', default_value='/dev/gps')
    baud_arg = DeclareLaunchArgument('gps_baud', default_value='38400')

    gps_config = Node(
        package='agv_bringup', executable='gps_config', name='gps_config',
        output='screen',
        parameters=[{'port': LaunchConfiguration('gps_port'),
                     'baud': LaunchConfiguration('gps_baud')}],
    )

    gps_driver = TimerAction(period=4.0, actions=[Node(
        package='agv_bringup', executable='gps_driver_node', name='gps_driver',
        output='screen',
        parameters=[{'port':     LaunchConfiguration('gps_port'),
                     'baud':     LaunchConfiguration('gps_baud'),
                     'frame_id': 'gps_link'}],
    )])

    ekf_gps = TimerAction(period=4.0, actions=[Node(
        package='robot_localization', executable='ekf_node', name='ekf_gps_node',
        output='screen',
        parameters=[ekf_gps_cfg],
        remappings=[('odometry/filtered', '/odometry/filtered')],
    )])

    navsat = TimerAction(period=9.0, actions=[Node(
        package='robot_localization', executable='navsat_transform_node',
        name='navsat_transform', output='screen',
        parameters=[navsat_cfg],
        remappings=[
            ('/imu/data',          '/camera/imu'),
            ('/odometry/filtered', '/odometry/filtered'),
            ('/odometry/gps',      '/odometry/gps'),
            ('/gps/filtered',      '/gps/filtered'),
        ],
    )])

    return LaunchDescription([port_arg, baud_arg, gps_config, gps_driver, ekf_gps, navsat])
