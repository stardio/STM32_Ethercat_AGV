"""
localization.launch.py — Intel RealSense + RTAB-Map (위치추정 모드, 기존 맵 사용)

실행:
  ros2 launch agv_bringup localization.launch.py db_path:=~/.ros/rtabmap.db
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
        output='screen',
    )

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
                # Localization mode — no new nodes added to map
                'Mem/IncrementalMemory':  'false',
                'Mem/InitWMWithAllNodes': 'true',
            },
        ],
        remappings=[
            ('rgb/image',        '/camera/realsense2_camera/color/image_raw'),
            ('rgb/camera_info',  '/camera/realsense2_camera/color/camera_info'),
            ('depth/image',      '/camera/realsense2_camera/aligned_depth_to_color/image_raw'),
            ('odom',             odom_topic),
        ],
        output='screen',
    )

    return LaunchDescription([
        DeclareLaunchArgument('db_path',    default_value='~/.ros/rtabmap.db',
                              description='Existing RTAB-Map database'),
        DeclareLaunchArgument('frame_id',   default_value='base_link'),
        DeclareLaunchArgument('odom_topic', default_value='/odom'),

        realsense_node,
        rtabmap_node,
    ])
