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
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    # Package directory
    pkg_cooper_ros_clip = FindPackageShare('cooper_ros_clip')

    # Default parameter file path
    default_params_file = PathJoinSubstitution([
        pkg_cooper_ros_clip,
        'params',
        'clip_params.yaml'
    ])

    # Launch arguments
    params_file_arg = DeclareLaunchArgument(
        'params_file',
        default_value=default_params_file,
        description='Path to parameter file for CLIP node'
    )

    image_encoder_path_arg = DeclareLaunchArgument(
        'image_encoder_path',
        default_value='',
        description='Path to image encoder model file'
    )

    text_encoder_path_arg = DeclareLaunchArgument(
        'text_encoder_path',
        default_value='',
        description='Path to text encoder model file'
    )

    text_embedding_path_arg = DeclareLaunchArgument(
        'text_embedding_path',
        default_value='',
        description='Path to text embedding model file'
    )

    vocab_path_arg = DeclareLaunchArgument(
        'vocab_path',
        default_value='',
        description='Path to vocab file'
    )



    # CLIP node
    clip_node = Node(
        package='cooper_ros_clip',
        executable='clip',
        name='clip',
        namespace='cooper_ros',
        parameters=[
            LaunchConfiguration('params_file'),
            {
                'image_encoder_path': LaunchConfiguration('image_encoder_path'),
                'text_encoder_path': LaunchConfiguration('text_encoder_path'),
                'text_embedding_path': LaunchConfiguration('text_embedding_path'),
                'vocab_path': LaunchConfiguration('vocab_path'),
            }
        ],
		remappings=[
            ("request", "clip_terminal/request")
        ],
        output='screen',
        emulate_tty=True,
    )

    return LaunchDescription([
        params_file_arg,
        image_encoder_path_arg,
        text_encoder_path_arg,
        text_embedding_path_arg,
        vocab_path_arg,
        clip_node,
    ])
