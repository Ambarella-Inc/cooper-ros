# Copyright (c) 2025 Ambarella International LP
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
from launch import LaunchDescription
from launch.substitutions import LaunchConfiguration
from launch.actions import DeclareLaunchArgument, ExecuteProcess, RegisterEventHandler, EmitEvent
from launch_ros.actions import Node
from launch.events import Shutdown
from launch.event_handlers import OnProcessExit
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    resnet50_dir = get_package_share_directory('cooper_ros_resnet50')

    model_path = LaunchConfiguration('model_path')
    label_path = LaunchConfiguration('label_path')
    top_k = LaunchConfiguration('top_k')

    input_path = LaunchConfiguration('input_path')
    fps = LaunchConfiguration('fps')
    width = LaunchConfiguration('width')
    height = LaunchConfiguration('height')
    loop_count = LaunchConfiguration('loop_count')
    output_dir = LaunchConfiguration('output_dir')
    encoding = LaunchConfiguration('encoding')

    declare_model_path_cmd = DeclareLaunchArgument(
        'model_path',
        default_value="",
        description='Full path to model cavalry binary file.')
    declare_label_path_cmd = DeclareLaunchArgument(
        'label_path',
        default_value="",
        description='Full path to label file.')
    declare_top_k_cmd = DeclareLaunchArgument(
        'top_k',
        default_value="5",
        description='Top K to display the result.')

    declare_input_path_cmd = DeclareLaunchArgument(
        'input_path',
        default_value="",
        description='Full path to image file/folder.')
    declare_fps_cmd = DeclareLaunchArgument(
        'fps',
        default_value="10.0",
        description='FPS to publish images.')
    declare_width_cmd = DeclareLaunchArgument(
        'width',
        default_value="0",
        description='Width of image.')
    declare_height_cmd = DeclareLaunchArgument(
        'height',
        default_value="0",
        description='Height of image.')
    declare_loop_count_cmd = DeclareLaunchArgument(
        'loop_count',
        default_value="0",
        description='Loop count of publishing images.')
    declare_output_dir_cmd = DeclareLaunchArgument(
        'output_dir',
        default_value="",
        description='Output directory.')
    declare_encoding_cmd = DeclareLaunchArgument(
        'encoding',
        default_value="bgr8",
        description='Encoding of image.')

    cv_file_node = Node(
        namespace= "cooper_ros", name = "cv_file",
        package='cooper_ros_cv_file', executable='cv_file', output='screen',
        parameters = [{'input_path': input_path}, {'fps': fps},
            {'width': width}, {'height': height},
            {'loop_count': loop_count}, {'output_dir': output_dir},
            {'encoding': encoding}],
        arguments=[],
        remappings=[
            ('classification', 'resnet50/classification'),
        ])

    return LaunchDescription([
        declare_model_path_cmd,
        declare_label_path_cmd,
        declare_top_k_cmd,

        declare_input_path_cmd,
        declare_fps_cmd,
        declare_width_cmd,
        declare_height_cmd,
        declare_loop_count_cmd,
        declare_output_dir_cmd,
        declare_encoding_cmd,
        Node(
            namespace= "cooper_ros", name = "resnet50",
            package='cooper_ros_resnet50', executable='resnet50', output='screen',
            parameters = [{'model_path': model_path, 'label_path': label_path},
                {'top_k': top_k}],
            arguments=[],
            remappings=[
                ('image', 'cv_file/image'),
            ]),
        cv_file_node,
        RegisterEventHandler(
            event_handler=OnProcessExit(
                target_action=cv_file_node,
                on_exit=[
                    EmitEvent(event=Shutdown())
                ],
            )
        ),
    ])
