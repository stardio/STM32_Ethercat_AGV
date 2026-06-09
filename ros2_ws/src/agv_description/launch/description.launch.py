from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, Command, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    pkg = FindPackageShare('agv_description')
    xacro_file = PathJoinSubstitution([pkg, 'urdf', 'agv.urdf.xacro'])

    camera_x     = LaunchConfiguration('camera_x',     default='0.25')
    camera_y     = LaunchConfiguration('camera_y',     default='0.0')
    camera_z     = LaunchConfiguration('camera_z',     default='0.30')
    camera_pitch = LaunchConfiguration('camera_pitch', default='0.0')

    robot_description = ParameterValue(Command([
        'xacro ', xacro_file,
        ' camera_x:=',     camera_x,
        ' camera_y:=',     camera_y,
        ' camera_z:=',     camera_z,
        ' camera_pitch:=', camera_pitch,
    ]), value_type=str)

    return LaunchDescription([
        DeclareLaunchArgument('camera_x',     default_value='0.25'),
        DeclareLaunchArgument('camera_y',     default_value='0.0'),
        DeclareLaunchArgument('camera_z',     default_value='0.30'),
        DeclareLaunchArgument('camera_pitch', default_value='0.0'),

        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            parameters=[{'robot_description': robot_description,
                         'use_sim_time': False}],
            output='screen',
        ),
    ])
