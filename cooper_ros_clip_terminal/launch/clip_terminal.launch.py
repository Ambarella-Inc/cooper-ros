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
from launch_ros.actions import Node


def generate_launch_description():
    # CLIP Terminal node
    clip_terminal_node = Node(
        package='cooper_ros_clip_terminal',
            executable='clip_terminal',
            name='clip_terminal_node',
            output='screen',
            emulate_tty=True,

        remappings=[
            ("image_path", "clip/image_path"),
            ("text", "clip/text"),
            ("performance", "clip/performance"),
        ],
    )

    return LaunchDescription([
        clip_terminal_node,
    ])
