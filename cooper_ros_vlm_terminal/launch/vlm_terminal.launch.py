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

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    # VLM Terminal node
    vlm_terminal_node = Node(
        namespace="cooper_ros", name="vlm_terminal",
        package='cooper_ros_vlm_terminal', executable='vlm_terminal', output='screen',
        parameters=[],
        arguments=[],
        remappings=[
            ('image', 'iav_canvas/rgb8'),
            ('response', 'llava/response'),
            ('performance', 'llava/performance'),
        ],
        emulate_tty=True,
    )

    # IAV OSD node (placeholder - would be launched separately)
    # This node would subscribe to 'efm' topic for image display

    return LaunchDescription([
        vlm_terminal_node,
    ])
