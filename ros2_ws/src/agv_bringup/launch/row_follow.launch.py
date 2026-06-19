"""
row_follow.launch.py — 나무 열 추종 전체 스택

포함 노드:
  1. hardware.launch.py  — STM32 브릿지 + URDF robot_state_publisher
  2. realsense2_camera   — RealSense D435i (depth 848×480 30fps + IMU)
  3. ekf_filter_node     — robot_localization (odom + IMU 융합 → /odometry/filtered)
  4. row_follower        — depth PID 열 추종 제어기 (→ /cmd_vel)

토픽 흐름:
  STM32 엔코더 → stm32_bridge_node → /odom ──────┐
  RealSense IMU → /camera/imu ───────────────────→ ekf_filter_node → /odometry/filtered
  RealSense depth → /camera/realsense2_camera/depth/image_rect_raw → row_follower → /cmd_vel
  /row_follow/enable (std_msgs/Bool) → row_follower 활성/비활성

실행:
  ros2 launch agv_bringup row_follow.launch.py

열 추종 시작/정지:
  ros2 topic pub --once /row_follow/enable std_msgs/Bool '{data: true}'
  ros2 topic pub --once /row_follow/enable std_msgs/Bool '{data: false}'

상태 모니터링:
  ros2 topic echo /row_follow/status
  ros2 topic echo /agv_status
"""
import subprocess
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def _realsense_present() -> bool:
    """Intel RealSense USB 장치(VID=8086) 연결 여부 확인."""
    try:
        result = subprocess.run(
            ['lsusb', '-d', '8086:'],
            capture_output=True, timeout=2
        )
        return result.returncode == 0 and b'8086' in result.stdout
    except Exception:
        return False


def generate_launch_description():
    # ── Launch arguments ────────────────────────────────────────────────
    ws_url             = LaunchConfiguration('ws_url',             default='ws://localhost:8765')
    wheel_base         = LaunchConfiguration('wheel_base',         default='0.60')
    wheel_radius       = LaunchConfiguration('wheel_radius',       default='0.15')
    max_linear_mps     = LaunchConfiguration('max_linear_mps',     default='0.40')
    stop_distance_m    = LaunchConfiguration('stop_distance_m',    default='0.80')
    row_end_distance_m = LaunchConfiguration('row_end_distance_m', default='4.00')
    kp                 = LaunchConfiguration('kp',                 default='0.50')
    ki                 = LaunchConfiguration('ki',                 default='0.01')
    kd                 = LaunchConfiguration('kd',                 default='0.10')
    yolo_model         = LaunchConfiguration('yolo_model',         default='yolov8n.pt')
    yolo_infer_hz      = LaunchConfiguration('yolo_infer_hz',      default='5.0')
    yolo_conf          = LaunchConfiguration('yolo_conf',          default='0.50')

    # ── Config file paths ───────────────────────────────────────────────
    camera_ok = _realsense_present()
    ekf_cfg_file = 'ekf.yaml' if camera_ok else 'ekf_odom_only.yaml'
    ekf_config = PathJoinSubstitution([
        FindPackageShare('agv_bringup'), 'config', ekf_cfg_file
    ])
    geofence_config = PathJoinSubstitution([
        FindPackageShare('agv_bringup'), 'config', 'geofence.yaml'
    ])

    # ── 1. STM32 브릿지 + URDF ──────────────────────────────────────────
    hardware_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([
                FindPackageShare('agv_bringup'), 'launch', 'hardware.launch.py'
            ])
        ]),
        launch_arguments={
            'ws_url':      ws_url,
            'wheel_base':  wheel_base,
            'wheel_radius': wheel_radius,
        }.items(),
    )

    # ── 2. RealSense D435i (카메라 연결된 경우에만) ──────────────────────
    realsense_node = Node(
        package='realsense2_camera',
        executable='realsense2_camera_node',
        name='realsense2_camera',
        namespace='camera',
        parameters=[{
            'enable_color':         True,
            'enable_depth':         True,
            'align_depth.enable':   True,
            'depth_module.profile': '848x480x30',
            'rgb_camera.profile':   '848x480x30',
            'enable_gyro':          True,
            'enable_accel':         True,
            'unite_imu_method':     '1',
            'pointcloud.enable':    False,
        }],
        output='screen',
    ) if camera_ok else None

    # ── 3. EKF (odom + IMU 융합) ────────────────────────────────────────
    ekf_node = Node(
        package='robot_localization',
        executable='ekf_node',
        name='ekf_filter_node',
        parameters=[ekf_config],
        remappings=[
            ('odometry/filtered', '/odometry/filtered'),
        ],
        output='screen',
    )

    # ── 4. 열 추종 제어기 ───────────────────────────────────────────────
    row_follower_node = Node(
        package='agv_bringup',
        executable='row_follower',
        name='row_follower',
        parameters=[{
            'depth_topic':          '/camera/realsense2_camera/depth/image_rect_raw',
            'max_linear_mps':       max_linear_mps,
            'stop_distance_m':      stop_distance_m,
            'row_end_distance_m':   row_end_distance_m,
            'kp':                   kp,
            'ki':                   ki,
            'kd':                   kd,
        }],
        output='screen',
    )

    # ── 5. navsat_transform (GPS ENU 변환, hardware의 gps_driver 의존)
    # ekf_filter_node가 /odometry/filtered를 발행한 후 시작 (t=9s)
    navsat_node = TimerAction(
        period=9.0,
        actions=[Node(
            package='robot_localization',
            executable='navsat_transform_node',
            name='navsat_transform',
            output='screen',
            parameters=[PathJoinSubstitution([
                FindPackageShare('agv_bringup'), 'config', 'navsat_transform.yaml'
            ])],
            remappings=[
                ('/imu/data',          '/camera/imu'),
                ('/odometry/filtered', '/odometry/filtered'),
                ('/odometry/gps',      '/odometry/gps'),
                ('/gps/filtered',      '/gps/filtered'),
            ],
        )],
    )

    # ── 6. 장애물 분류기 (YOLOv8 + Geofence) ───────────────────────────
    obstacle_node = Node(
        package='agv_bringup',
        executable='obstacle_classifier',
        name='obstacle_classifier',
        parameters=[{
            'model_path':       yolo_model,
            'infer_hz':         yolo_infer_hz,
            'conf_threshold':   yolo_conf,
            'max_react_dist_m': 3.0,
            'geofence_config':  geofence_config,
            'odom_topic':       '/odometry/filtered',
        }],
        output='screen',
    )

    nodes = [
        hardware_launch,
        ekf_node,
        navsat_node,
        obstacle_node,
        row_follower_node,
    ]
    if realsense_node is not None:
        nodes.insert(1, realsense_node)
        print('[row_follow] RealSense D435i 감지됨 — 카메라+IMU 모드로 실행')
    else:
        print('[row_follow] RealSense 미연결 — 오도메트리 전용 EKF로 실행')

    return LaunchDescription([
        # ── Arguments ──────────────────────────────────────────────────
        DeclareLaunchArgument('ws_url',
                              default_value='ws://localhost:8765',
                              description='bridge.py WebSocket URL'),
        DeclareLaunchArgument('wheel_base',
                              default_value='0.60',
                              description='바퀴 간격 [m]'),
        DeclareLaunchArgument('wheel_radius',
                              default_value='0.15',
                              description='바퀴 반경 [m]'),
        DeclareLaunchArgument('max_linear_mps',
                              default_value='0.40',
                              description='최대 전진 속도 [m/s]'),
        DeclareLaunchArgument('stop_distance_m',
                              default_value='0.80',
                              description='전방 장애물 정지 거리 [m]'),
        DeclareLaunchArgument('row_end_distance_m',
                              default_value='4.00',
                              description='열 끝 판정 거리 [m]'),
        DeclareLaunchArgument('kp', default_value='0.50', description='PID Kp'),
        DeclareLaunchArgument('ki', default_value='0.01', description='PID Ki'),
        DeclareLaunchArgument('kd', default_value='0.10', description='PID Kd'),
        DeclareLaunchArgument('yolo_model',
                              default_value='yolov8n.pt',
                              description='YOLOv8 모델 (yolov8n/s/m.pt)'),
        DeclareLaunchArgument('yolo_infer_hz',
                              default_value='5.0',
                              description='YOLOv8 추론 주기 [Hz]'),
        DeclareLaunchArgument('yolo_conf',
                              default_value='0.50',
                              description='YOLOv8 confidence threshold'),

        *nodes,
    ])
