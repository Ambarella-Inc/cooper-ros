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
    resnet50_dir = get_package_share_directory('cooper_ros_resnet50')
    iav_osd_dir = get_package_share_directory('cooper_ros_iav_osd')

    model_path = LaunchConfiguration('model_path')
    label_path = LaunchConfiguration('label_path')
    top_k = LaunchConfiguration('top_k')

    media = LaunchConfiguration('media')

    canvas_id = LaunchConfiguration('canvas_id')
    fps = LaunchConfiguration('fps')
    enable_bgr8 = LaunchConfiguration('enable_bgr8')
    width_bgr8 = LaunchConfiguration('width_bgr8')
    height_bgr8 = LaunchConfiguration('height_bgr8')

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

    declare_media_cmd = DeclareLaunchArgument(
        'media',
        default_value="stream:0",
        description='OSD media to display the result.')

    declare_canvas_id_cmd = DeclareLaunchArgument(
        'canvas_id',
        default_value="1",
        description='Canvas ID to query the cameraimage.')
    declare_fps_cmd = DeclareLaunchArgument(
        'fps',
        default_value="30.0",
        description='FPS to query the cameraimage.')
    declare_enable_bgr8_cmd = DeclareLaunchArgument(
        'enable_bgr8',
        default_value="true",
        description='Enable publishing BGR image data.')
    declare_width_bgr8_cmd = DeclareLaunchArgument(
        'width_bgr8',
        default_value="224",
        description='Width of BGR image data.')
    declare_height_bgr8_cmd = DeclareLaunchArgument(
        'height_bgr8',
        default_value="224",
        description='Height of BGR image data.')

    iav_osd_params = os.path.join(iav_osd_dir, 'params', 'iav_osd_params.yaml')

    return LaunchDescription([
        declare_model_path_cmd,
        declare_label_path_cmd,
        declare_top_k_cmd,

        declare_media_cmd,
        declare_canvas_id_cmd,
        declare_fps_cmd,
        declare_enable_bgr8_cmd,
        declare_width_bgr8_cmd,
        declare_height_bgr8_cmd,
        Node(
            namespace= "cooper_ros", name = "resnet50",
            package='cooper_ros_resnet50', executable='resnet50', output='screen',
            parameters = [{'model_path': model_path}, {'label_path': label_path},
                {'top_k': top_k}],
            arguments=[],
            remappings=[
                ('image', 'iav_canvas/bgr8'),
            ]),
        Node(
            namespace= "cooper_ros", name = "iav_osd",
            package='cooper_ros_iav_osd', executable='iav_osd', output='screen',
            parameters = [iav_osd_params, {'media': media}],
            arguments=[],
            remappings=[
                ('classification', 'resnet50/classification'),
            ]),
        Node(
            namespace= "cooper_ros", name = "iav_canvas",
            package='cooper_ros_iav_canvas', executable='iav_canvas', output='screen',
            parameters = [{'canvas_id': canvas_id}, {'fps': fps},
                {'enable_bgr8': enable_bgr8}, {'width_bgr8': width_bgr8}, {'height_bgr8': height_bgr8}],
            arguments=[],
            remappings=[]),
    ])
