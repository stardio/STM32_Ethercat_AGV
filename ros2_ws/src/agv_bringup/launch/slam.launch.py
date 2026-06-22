"""
slam.launch.py — Intel RealSense D435/D455 + RTAB-Map (맵핑 모드)

실행:
  ros2 launch agv_bringup slam.launch.py
  ros2 launch agv_bringup slam.launch.py db_path:=/path/to/map.db
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    db_path    = LaunchConfiguration('db_path',    default='~/.ros/rtabmap.db')
    frame_id   = LaunchConfiguration('frame_id',   default='base_link')
    odom_topic = LaunchConfiguration('odom_topic', default='/odom')

    rtabmap_params = PathJoinSubstitution([
        FindPackageShare('agv_bringup'), 'config', 'rtabmap_params.yaml'
    ])

    # ── Intel RealSense D435/D455 ─────────────────────────────────────────
    realsense_node = Node(
        package='realsense2_camera',
        executable='realsense2_camera_node',
        name='realsense2_camera',
        namespace='camera',
        parameters=[{
            'enable_color':          True,
            'enable_depth':          True,
            'enable_gyro':           True,
            'enable_accel':          True,
            'unite_imu_method':      '1',
            'align_depth.enable':    True,
            'depth_module.profile':  '640x480x15',
            'rgb_camera.profile':    '640x480x15',
            'pointcloud.enable':     False,
        }],
        remappings=[],
        output='screen',
    )

    # ── RTAB-Map ─────────────────────────────────────────────────────────
    rtabmap_node = Node(
        package='rtabmap_slam',
        executable='rtabmap',
        name='rtabmap',
        parameters=[
            rtabmap_params,
            {
                'frame_id':      frame_id,
                'odom_frame_id': 'odom',
                'database_path': db_path,
                # SLAM mode — incremental memory ON
                'Mem/IncrementalMemory': 'true',
                'Mem/InitWMWithAllNodes': 'false',
            },
        ],
        remappings=[
            ('rgb/image',        '/camera/realsense2_camera/color/image_raw'),
            ('rgb/camera_info',  '/camera/realsense2_camera/color/camera_info'),
            ('depth/image',      '/camera/realsense2_camera/aligned_depth_to_color/image_raw'),
            ('odom',             odom_topic),
        ],
        arguments=['--delete_db_on_start'],
        output='screen',
    )

    # ── RTAB-Map RViz2 시각화 (선택) ─────────────────────────────────────
    rtabmap_viz = Node(
        package='rtabmap_viz',
        executable='rtabmap_viz',
        name='rtabmap_viz',
        parameters=[rtabmap_params],
        remappings=[
            ('rgb/image',       '/camera/realsense2_camera/color/image_raw'),
            ('rgb/camera_info', '/camera/realsense2_camera/color/camera_info'),
            ('depth/image',     '/camera/realsense2_camera/aligned_depth_to_color/image_raw'),
            ('odom',            odom_topic),
        ],
        output='screen',
    )

    return LaunchDescription([
        DeclareLaunchArgument('db_path',    default_value='~/.ros/rtabmap.db',
                              description='RTAB-Map database path'),
        DeclareLaunchArgument('frame_id',   default_value='base_link'),
        DeclareLaunchArgument('odom_topic', default_value='/odom'),

        realsense_node,
        rtabmap_node,
        rtabmap_viz,
    ])
