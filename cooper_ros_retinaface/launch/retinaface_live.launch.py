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
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    retinaface_dir = get_package_share_directory('cooper_ros_retinaface')
    iav_osd_dir = get_package_share_directory('cooper_ros_iav_osd')

    model_path = LaunchConfiguration('model_path')

    media = LaunchConfiguration('media')
    enable_256_colors = LaunchConfiguration('enable_256_colors')
    has_landmark = LaunchConfiguration('has_landmark')

    canvas_id = LaunchConfiguration('canvas_id')
    fps = LaunchConfiguration('fps')
    enable_rgb8 = LaunchConfiguration('enable_rgb8')
    width_rgb8 = LaunchConfiguration('width_rgb8')
    height_rgb8 = LaunchConfiguration('height_rgb8')

    declare_model_path_cmd = DeclareLaunchArgument(
        'model_path',
        default_value="",
        description='Full path to model cavalry binary file.')

    declare_media_cmd = DeclareLaunchArgument(
        'media',
        default_value="stream:0",
        description='OSD media to display the result.')
    declare_has_landmark_cmd = DeclareLaunchArgument(
        'has_landmark',
        default_value="false",
        description='Enable landmark detection.')

    declare_canvas_id_cmd = DeclareLaunchArgument(
        'canvas_id',
        default_value="1",
        description='Canvas ID to query the cameraimage.')
    declare_fps_cmd = DeclareLaunchArgument(
        'fps',
        default_value="30.0",
        description='FPS to query the cameraimage.')
    declare_enable_rgb8_cmd = DeclareLaunchArgument(
        'enable_rgb8',
        default_value="true",
        description='Enable publishing RGB image data.')
    declare_width_rgb8_cmd = DeclareLaunchArgument(
        'width_rgb8',
        default_value="0",
        description='Width of RGB image data.')
    declare_height_rgb8_cmd = DeclareLaunchArgument(
        'height_rgb8',
        default_value="0",
        description='Height of RGB image data.')

    retinaface_params = os.path.join(retinaface_dir, 'params', 'retinaface_params.yaml')
    iav_osd_params = os.path.join(iav_osd_dir, 'params', 'iav_osd_params.yaml')

    return LaunchDescription([
        declare_model_path_cmd,

        declare_media_cmd,
        declare_has_landmark_cmd,
        declare_canvas_id_cmd,
        declare_fps_cmd,
        declare_enable_rgb8_cmd,
        declare_width_rgb8_cmd,
        declare_height_rgb8_cmd,
        Node(
            namespace= "cooper_ros", name = "retinaface",
            package='cooper_ros_retinaface', executable='retinaface', output='screen',
            parameters = [retinaface_params, {'model_path': model_path}],
            arguments=[],
            remappings=[
                ('image', 'iav_canvas/rgb8'),
            ]),
        Node(
            namespace= "cooper_ros", name = "iav_osd",
            package='cooper_ros_iav_osd', executable='iav_osd', output='screen',
            parameters = [iav_osd_params, {'media': media}, {'has_landmark': has_landmark}],
            arguments=[],
            remappings=[
                ('detections', 'retinaface/detections'),
                ('landmarks', 'retinaface/landmarks'),
            ]),
        Node(
            namespace= "cooper_ros", name = "iav_canvas",
            package='cooper_ros_iav_canvas', executable='iav_canvas', output='screen',
            parameters = [{'canvas_id': canvas_id}, {'fps': fps},
                {'enable_rgb8': enable_rgb8}, {'width_rgb8': width_rgb8}, {'height_rgb8': height_rgb8},
                ],
            arguments=[],
            remappings=[]),
    ])
