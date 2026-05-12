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

#ifndef COOPER_ROS__IAV_OSD__IAV_OSD_NODE_HPP_
#define COOPER_ROS__IAV_OSD__IAV_OSD_NODE_HPP_

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>
#include <cooper_ros_msgs/msg/landmark_detection2_d_array.hpp>
#include <vision_msgs/msg/classification.hpp>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/time_synchronizer.h>

#include <eazyai.h>
#include <eazyai_io.h>

namespace cooper_ros
{
namespace iav_osd
{

class IavOsdNode : public rclcpp::Node
{
public:
  IavOsdNode(const rclcpp::NodeOptions & options);
  virtual ~IavOsdNode();

private:
  void efmImageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void detectionCallback(const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg);
  void faceDetectionCallback(
    const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg,
    const cooper_ros_msgs::msg::LandmarkDetection2DArray::ConstSharedPtr & landmark_msg);
  void classificationCallback(const vision_msgs::msg::Classification::ConstSharedPtr & msg);
  void segmentationCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void timerCallback();
  void drawDetectionResult(const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg);
  void drawSegmentationResult(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void drawSegmentationResultCleanup();
  void drawFaceDetectionResult(
    const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg,
    const cooper_ros_msgs::msg::LandmarkDetection2DArray::ConstSharedPtr & landmark_msg);
  void drawClassificationResult(const vision_msgs::msg::Classification::ConstSharedPtr & msg);
  void initIavOsd();
  void init256ColorTable();

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

  static int draw256ColorsImage(
    uint8_t * buffer, int w, int h, int pitch, int rotate_type, void * arg);

  std::string osd_type_;
  int stream_id_;
  int timer_fps_;
  ea_display_t * display_;
  ea_io_efm_t * io_efm_;
  bool enable_segmentation_;
  ea_tensor_t * segmentation_tensor_;

  std::mutex msg_mutex_;
  std::vector<sensor_msgs::msg::Image::ConstSharedPtr> segmentation_list_;
  std::vector<vision_msgs::msg::Detection2DArray::ConstSharedPtr> detection_list_;
  std::vector<std::pair<
    vision_msgs::msg::Detection2DArray::ConstSharedPtr,
    cooper_ros_msgs::msg::LandmarkDetection2DArray::ConstSharedPtr>>
    face_detection_list_;
  std::vector<vision_msgs::msg::Classification::ConstSharedPtr> classification_list_;
  rclcpp::TimerBase::SharedPtr timer_;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr efm_image_subscription_;
  rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr detection_subscription_;
  rclcpp::Subscription<vision_msgs::msg::Classification>::SharedPtr classification_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr segmentation_subscription_;

  using ExactTimePolicy = message_filters::sync_policies::
    ExactTime<vision_msgs::msg::Detection2DArray, cooper_ros_msgs::msg::LandmarkDetection2DArray>;
  std::shared_ptr<message_filters::Synchronizer<ExactTimePolicy>> exact_time_synchronizer_;
  std::shared_ptr<message_filters::Subscriber<vision_msgs::msg::Detection2DArray>>
    detection_subscriber_;
  std::shared_ptr<message_filters::Subscriber<cooper_ros_msgs::msg::LandmarkDetection2DArray>>
    landmark_subscriber_;
};

}  // namespace iav_osd
}  // namespace cooper_ros

#endif  // COOPER_ROS__IAV_OSD_NODE_HPP_
