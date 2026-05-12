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

#ifndef COOPER_ROS__IAV_CANVAS__IAV_CANVAS_NODE_HPP_
#define COOPER_ROS__IAV_CANVAS__IAV_CANVAS_NODE_HPP_

#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <eazyai.h>
#include <iav_ioctl.h>

namespace cooper_ros
{
namespace iav_canvas
{

class IavCanvasNode : public rclcpp::Node
{
public:
  IavCanvasNode(const rclcpp::NodeOptions & options);
  virtual ~IavCanvasNode();

private:
  void updatePublishers();
  void publishNv12(ea_tensor_t * img_data_tensor);
  void publishRgb(ea_tensor_t * img_data_tensor);
  void publishBgr(ea_tensor_t * img_data_tensor);
  void publishMono(ea_tensor_t * img_data_tensor);
  void publishImgNoWait();
  rcl_interfaces::msg::SetParametersResult onSetParameters(
    const std::vector<rclcpp::Parameter> & parameters);
  int getCanvasSize();
  int copyTensorToImage(ea_tensor_t * tensor, sensor_msgs::msg::Image::SharedPtr & image);

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr nv12_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr rgb_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr bgr_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr mono_publisher_;
  int canvas_id_ = 0;
  int old_canvas_id_ = 0;
  double fps_ = 30.0;
  double old_fps_ = 30.0;
  bool enable_nv12_ = false;
  bool enable_rgb8_ = false;
  bool enable_bgr8_ = false;
  bool enable_mono8_ = false;
  int width_rgb8_ = 0;
  int height_rgb8_ = 0;
  int width_bgr8_ = 0;
  int height_bgr8_ = 0;
  int width_mono8_ = 0;
  int height_mono8_ = 0;
  int canvas_width_ = 0;
  int canvas_height_ = 0;
  ea_img_resource_t * img_resource_ = NULL;
  std::mutex param_mutex_;
  OnSetParametersCallbackHandle::SharedPtr param_callback_handle_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace iav_canvas
}  // namespace cooper_ros

#endif  // COOPER_ROS__IAV_CANVAS__IAV_CANVAS_NODE_HPP_
