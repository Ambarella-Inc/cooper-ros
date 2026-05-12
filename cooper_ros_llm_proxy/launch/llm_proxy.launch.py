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

"""
Launch file for LLM Proxy using topic communication
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    """
    Generate launch description for LLM Proxy
    """

    # Get the package directory
    package_dir = get_package_share_directory('cooper_ros_llm_proxy')

    # Path to config file
    config_file = PathJoinSubstitution([
        FindPackageShare('cooper_ros_llm_proxy'),
        'config',
        'bailian.yaml'
    ])

    # Declare launch arguments
    config_file_arg = DeclareLaunchArgument(
        'config_file',
        default_value=config_file,
        description='Path to the config file'
    )

    # API configuration arguments
    api_key_arg = DeclareLaunchArgument(
        'api_key',
        default_value='',
        description='LLM service API key'
    )

    # LLM Proxy Service Node
    llm_proxy_service_node = Node(
        package='cooper_ros_llm_proxy',
        executable='llm_proxy_service',
        name='llm_proxy_service',
        namespace='cooper_ros',
        output='screen',
        parameters=[
            LaunchConfiguration('config_file'),
            {
                'api_key': LaunchConfiguration('api_key'),
            }
        ],
        remappings=[
            ('user_content', 'llm_proxy/user_content'),
        ],
        emulate_tty=True,
    )

    # LLM Proxy Terminal Node (optional, can be started separately)
    llm_proxy_terminal_node = Node(
        package='cooper_ros_llm_proxy',
        executable='llm_proxy_terminal',
        name='llm_proxy_terminal',
        namespace='cooper_ros',
        output='screen',
        parameters=[
            LaunchConfiguration('config_file'),
        ],
        remappings=[
            ('response', 'llm_proxy/response'),
        ],
        emulate_tty=True,
    )

    return LaunchDescription([
        config_file_arg,
        api_key_arg,
        llm_proxy_service_node,
        # Comment out the terminal node if you want to run it separately
        # llm_proxy_terminal_node,
    ])
