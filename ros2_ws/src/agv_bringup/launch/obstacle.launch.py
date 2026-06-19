"""
obstacle.launch.py — 장애물 감지 단독 테스트 launch

YOLOv8 장애물 분류기만 단독으로 실행한다 (RealSense 카메라 포함).
row_follower 없이 obstacle_classifier 동작을 검증할 때 사용.

포함 노드:
  1. realsense2_camera   — RealSense D435i (color + depth)
  2. obstacle_classifier — YOLOv8 + Geofence 안전 레이어

실행:
  ros2 launch agv_bringup obstacle.launch.py

모니터링:
  ros2 topic echo /obstacle/action
  ros2 topic echo /obstacle/detections
  ros2 topic hz   /obstacle/action

디버그 영상 (YOLOv8 bounding box 시각화):
  ros2 run rqt_image_view rqt_image_view /obstacle/debug_image

열 추종과 통합 실행은 row_follow.launch.py 사용.
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    model_path       = LaunchConfiguration('model_path',       default='yolov8n.pt')
    infer_hz         = LaunchConfiguration('infer_hz',         default='5.0')
    conf_threshold   = LaunchConfiguration('conf_threshold',   default='0.50')
    max_react_dist_m = LaunchConfiguration('max_react_dist_m', default='3.00')
    geofence_config  = LaunchConfiguration('geofence_config',  default='')

    geofence_yaml = PathJoinSubstitution([
        FindPackageShare('agv_bringup'), 'config', 'geofence.yaml'
    ])

    # ── RealSense D435i ─────────────────────────────────────────────────
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
    )

    # ── Obstacle Classifier ─────────────────────────────────────────────
    obstacle_node = Node(
        package='agv_bringup',
        executable='obstacle_classifier',
        name='obstacle_classifier',
        parameters=[{
            'model_path':       model_path,
            'infer_hz':         infer_hz,
            'conf_threshold':   conf_threshold,
            'max_react_dist_m': max_react_dist_m,
            'geofence_config':  geofence_yaml,
            'odom_topic':       '/odometry/filtered',
        }],
        output='screen',
    )

    return LaunchDescription([
        DeclareLaunchArgument('model_path',
                              default_value='yolov8n.pt',
                              description='YOLOv8 모델 파일 경로 (yolov8n/s/m/l/x.pt)'),
        DeclareLaunchArgument('infer_hz',
                              default_value='5.0',
                              description='YOLOv8 추론 주기 [Hz]'),
        DeclareLaunchArgument('conf_threshold',
                              default_value='0.50',
                              description='YOLOv8 confidence threshold'),
        DeclareLaunchArgument('max_react_dist_m',
                              default_value='3.00',
                              description='장애물 반응 최대 거리 [m]'),
        DeclareLaunchArgument('geofence_config',
                              default_value='',
                              description='geofence.yaml 경로 (빈 문자열=비활성)'),

        realsense_node,
        obstacle_node,
    ])
