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
    yolox_dir = get_package_share_directory('cooper_ros_yolox')

    model_path = LaunchConfiguration('model_path')
    label_path = LaunchConfiguration('label_path')
    class_agnostic = LaunchConfiguration('class_agnostic')

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
    declare_class_agnostic_cmd = DeclareLaunchArgument(
        'class_agnostic',
        default_value="true",
        description='Use multi class or best class in post process. false=multi class, true=best class.')

    declare_input_path_cmd = DeclareLaunchArgument(
        'input_path',
        default_value="",
        description='Full path to image file/folder.')
    declare_fps_cmd = DeclareLaunchArgument(
        'fps',
        default_value="30.0",
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
        default_value="1",
        description='Loop count of publishing images.')
    declare_output_dir_cmd = DeclareLaunchArgument(
        'output_dir',
        default_value="",
        description='Output directory.')
    declare_encoding_cmd = DeclareLaunchArgument(
        'encoding',
        default_value="rgb8",
        description='Encoding of image.')

    yolox_params = os.path.join(yolox_dir, 'params', 'yolox_params.yaml')

    cv_file_node = Node(
        namespace= "cooper_ros", name = "cv_file",
        package='cooper_ros_cv_file', executable='cv_file', output='screen',
        parameters = [{'input_path': input_path}, {'fps': fps},
            {'width': width}, {'height': height},
            {'loop_count': loop_count}, {'output_dir': output_dir},
            {'encoding': encoding}],
        arguments=[],
        remappings=[
            ('detections', 'yolox/detections'),
        ])

    return LaunchDescription([
        declare_model_path_cmd,
        declare_label_path_cmd,
        declare_class_agnostic_cmd,
        declare_input_path_cmd,
        declare_fps_cmd,
        declare_width_cmd,
        declare_height_cmd,
        declare_loop_count_cmd,
        declare_output_dir_cmd,
        declare_encoding_cmd,
        Node(
            namespace= "cooper_ros", name = "yolox",
            package='cooper_ros_yolox', executable='yolox', output='screen',
            parameters = [
                yolox_params,
                {'model_path': model_path},
                {'label_path': label_path},
                {'class_agnostic': class_agnostic},
            ],
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
