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
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    # Package directory
    llava_dir = get_package_share_directory('cooper_ros_llava')
    iav_osd_dir = get_package_share_directory('cooper_ros_iav_osd')

    iav_osd_params = os.path.join(iav_osd_dir, 'params', 'iav_osd_params.yaml')
    llava_params = os.path.join(llava_dir, 'params', 'llava_params.yaml')

    media = LaunchConfiguration('media')
    enable_efm = LaunchConfiguration('enable_efm')

    canvas_id = LaunchConfiguration('canvas_id')
    fps = LaunchConfiguration('fps')
    enable_rgb8 = LaunchConfiguration('enable_rgb8')
    width_rgb8 = LaunchConfiguration('width_rgb8')
    height_rgb8 = LaunchConfiguration('height_rgb8')

    declare_media_cmd = DeclareLaunchArgument(
        'media',
        default_value="stream:1",
        description='OSD media to display the result.')
    declare_enable_efm_cmd = DeclareLaunchArgument(
        'enable_efm',
        default_value="true",
        description='Enable EFM encoding.')

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
        default_value="720",
        description='Width of RGB image data.')
    declare_height_rgb8_cmd = DeclareLaunchArgument(
        'height_rgb8',
        default_value="480",
        description='Height of RGB image data.')

    declare_params_file_cmd = DeclareLaunchArgument(
        'params_file',
        default_value=llava_params,
        description='Path to parameter file for LLaVA node.')

    declare_vit_path_cmd = DeclareLaunchArgument(
        'vit_path',
        default_value='',
        description='Path to Vision Transformer model file.')

    declare_vit_video_path_cmd = DeclareLaunchArgument(
        'vit_video_path',
        default_value='',
        description='Path to Video Vision Transformer model file.')

    declare_vit_single_path_cmd = DeclareLaunchArgument(
        'vit_single_path',
        default_value='',
        description='Path to Single Vision Transformer model file.')

    declare_base_path_cmd = DeclareLaunchArgument(
        'base_path',
        default_value='',
        description='Path to model weight base folder.')

    declare_llm_path_cmd = DeclareLaunchArgument(
        'llm_path',
        default_value='',
        description='Path to Large Language Model file.')

    declare_device_cmd = DeclareLaunchArgument(
        'device',
        default_value='2',
        description='Device ID for model inference.')

    declare_llm_mode_cmd = DeclareLaunchArgument(
        'llm_mode',
        default_value='1',
        description='LLM mode (0: LLAVA, 1: LLAVA_OV).')

    return LaunchDescription([
        declare_params_file_cmd,
        declare_vit_path_cmd,
        declare_vit_video_path_cmd,
        declare_vit_single_path_cmd,
        declare_base_path_cmd,
        declare_llm_path_cmd,
        declare_device_cmd,
        declare_llm_mode_cmd,
        declare_media_cmd,
        declare_enable_efm_cmd,
        declare_canvas_id_cmd,
        declare_fps_cmd,
        declare_enable_rgb8_cmd,
        declare_width_rgb8_cmd,
        declare_height_rgb8_cmd,
        Node(
            namespace="cooper_ros", name="llava",
            package='cooper_ros_llava', executable='llava', output='screen',
            parameters=[
                LaunchConfiguration('params_file'),
                {
                    'vit_path': LaunchConfiguration('vit_path'),
                    'vit_video_path': LaunchConfiguration('vit_video_path'),
                    'vit_single_path': LaunchConfiguration('vit_single_path'),
                    'base_path': LaunchConfiguration('base_path'),
                    'llm_path': LaunchConfiguration('llm_path'),
                    'device': LaunchConfiguration('device'),
                    'llm_mode': LaunchConfiguration('llm_mode'),
                }
            ],
            arguments=[],
            remappings=[
                ('image', 'vlm_terminal/image_captured'),
                ('user_content', 'vlm_terminal/user_content'),
            ],
        ),
        Node(
            namespace= "cooper_ros", name = "iav_osd",
            package='cooper_ros_iav_osd', executable='iav_osd', output='screen',
            parameters = [iav_osd_params, {'media': media}, {'enable_efm': enable_efm}],
            arguments=[],
            remappings=[('efm_image', 'vlm_terminal/image_captured')],
        ),
        Node(
            namespace= "cooper_ros", name = "iav_canvas",
            package='cooper_ros_iav_canvas', executable='iav_canvas', output='screen',
            parameters = [{'canvas_id': canvas_id}, {'fps': fps},
                {'enable_rgb8': enable_rgb8}, {'width_rgb8': width_rgb8}, {'height_rgb8': height_rgb8},
                ],
            arguments=[],
            remappings=[],
        ),
    ])
