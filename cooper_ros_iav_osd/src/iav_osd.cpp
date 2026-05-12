// Copyright (c) 2025 Ambarella International LP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cooper_ros_iav_osd/iav_osd_node.hpp"

#include <memory>

#include <eazyai.h>

int main(int argc, char ** argv)
{
  // Force flush of the stdout buffer.
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  int rval = ea_env_open(
    EA_ENV_ENABLE_IAV | EA_ENV_ENABLE_VPROC | EA_ENV_ENABLE_OSD_VOUT | EA_ENV_ENABLE_OSD_STREAM);
  if (rval < 0) {
    throw std::runtime_error("ea_env_open failed, error: " + std::to_string(rval));
  }

  ea_utils_env_callbacks_register();

  do {
    rclcpp::init(argc, argv);
    rclcpp::NodeOptions options;
    auto node = std::make_shared<cooper_ros::iav_osd::IavOsdNode>(options);

    rclcpp::spin(node);
    rclcpp::shutdown();
  } while (0);

  ea_env_close();

  return 0;
}
