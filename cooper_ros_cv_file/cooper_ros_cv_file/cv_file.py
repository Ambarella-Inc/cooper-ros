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

import sys
import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node

from cooper_ros_cv_file.cv_file_node import CvFileNode


def main(args=None):
    try:
        rclpy.init(args=args)

        cv_file_node = CvFileNode()

        rclpy.spin(cv_file_node)

        cv_file_node.destroy_node()
    except (KeyboardInterrupt, ExternalShutdownException):
        pass


if __name__ == '__main__':
    main()
