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

#ifndef COOPER_ROS__RESNET50__RESNET50_NODE_HPP_
#define COOPER_ROS__RESNET50__RESNET50_NODE_HPP_

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <vision_msgs/msg/classification.hpp>
#include <std_msgs/msg/string.hpp>

#include <eazyai.h>

#include "resnet50_net.h"

namespace cooper_ros
{

namespace resnet50
{

class ResNet50Node : public rclcpp::Node
{
public:
  ResNet50Node(const rclcpp::NodeOptions & options);
  virtual ~ResNet50Node();

private:
  void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void initNetwork();

  std::string declareString(
    const std::string & name, const std::string & description = "", bool ignore_override = false)
  {
    rcl_interfaces::msg::ParameterDescriptor desc;
    desc.description = description;

    std::string param = this->declare_parameter(name, std::string(""), desc, ignore_override);
    if (param.size() == 0) {
      throw rclcpp::exceptions::InvalidParameterValueException(
        "parameter " + name + " must be non-empty. got: " + param);
    }

    return param;
  }

  static int copyImageToTensor(
    const sensor_msgs::msg::Image::ConstSharedPtr & image,
    ea_tensor_t * tensor,
    bool swap_rb = false);

  static int copyTensorToImage(ea_tensor_t * tensor, sensor_msgs::msg::Image::SharedPtr & image);

  resnet50_t * resnet50_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscription_;
  rclcpp::Publisher<vision_msgs::msg::Classification>::SharedPtr classification_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr performance_publisher_;
  ea_calc_fps_ctx_t fps_ctx_;
  float last_fps_;
};

}  // namespace resnet50
}  // namespace cooper_ros

#endif  // COOPER_ROS__RETINAFACE_NODE_HPP_
