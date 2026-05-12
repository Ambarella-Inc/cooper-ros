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

#include "cooper_ros_resnet50/resnet50_node.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp/exceptions.hpp>

#include <nlohmann/json.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>

#ifdef USE_CV_BRIDGE_H
#include <cv_bridge/cv_bridge.h>
#else
#include <cv_bridge/cv_bridge.hpp>
#endif

#define PUB_TOPIC_NAME_CLASSIFICATION "resnet50/classification"
#define PUB_TOPIC_NAME_PERFORMANCE    "resnet50/performance"
#define SUB_TOPIC_NAME_IMAGE          "image"

#define PARAM_NAME_MODEL_PATH "model_path"
#define PARAM_NAME_LABEL_PATH "label_path"
#define PARAM_NAME_TOP_K      "top_k"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

namespace cooper_ros
{
namespace resnet50
{
ResNet50Node::ResNet50Node(const rclcpp::NodeOptions & options)
: rclcpp::Node("ResNet50Node", options)
{
  initNetwork();

  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  image_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
    SUB_TOPIC_NAME_IMAGE,
    reliable_qos,
    std::bind(&ResNet50Node::imageCallback, this, std::placeholders::_1));

  classification_publisher_ = this->create_publisher<vision_msgs::msg::Classification>(
    PUB_TOPIC_NAME_CLASSIFICATION,
    reliable_qos);
  performance_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_PERFORMANCE, reliable_qos);

  memset(&fps_ctx_, 0, sizeof(ea_calc_fps_ctx_t));
  fps_ctx_.count_period = 0;  // will calculate fps every 1 second
  last_fps_ = 0.0;
}

ResNet50Node::~ResNet50Node()
{
  if (resnet50_) {
    resnet50_free(resnet50_);
    resnet50_ = NULL;
  }
}

void ResNet50Node::initNetwork()
{
  int rval = 0;

  do {
    std::string model_path = declareString(PARAM_NAME_MODEL_PATH);
    std::string label_path = declareString(PARAM_NAME_LABEL_PATH);
    int top_k = this->declare_parameter(PARAM_NAME_TOP_K, 5);

    resnet50_params_t net_params;
    memset(&net_params, 0, sizeof(resnet50_params_t));
    net_params.log_level = EA_LOG_LEVEL_NOTICE;
    net_params.model_path = model_path.c_str();
    net_params.label_path = label_path.c_str();
    net_params.top_k = top_k;
    resnet50_ = resnet50_new(&net_params);
    EA_R_OK(resnet50_ != NULL);
  } while (0);

  if (rval < 0) {
    throw std::runtime_error("ResNet50Node::Init() failed.");
  }
}

int ResNet50Node::copyImageToTensor(
  const sensor_msgs::msg::Image::ConstSharedPtr & image, ea_tensor_t * tensor, bool swap_rb)
{
  int rval = 0;
  const size_t * shape = ea_tensor_shape(tensor);
  const size_t pitch = ea_tensor_pitch(tensor);
  uint8_t * tensor_data = (uint8_t *)ea_tensor_data_for_write(tensor, EA_CPU);
  std::vector<cv::Mat> channels(3);
  int i;

  do {
    EA_R_ASSERT(
      image->encoding == "rgb8" || image->encoding == "bgr8" || image->encoding == "mono8");
    EA_R_ASSERT(image->width == shape[EA_W] && image->height == shape[EA_H]);

    cv::Mat image_mat = cv_bridge::toCvShare(image, image->encoding)->image;
    for (i = 0; i < image_mat.channels(); i++) {
      channels[i] = cv::Mat(
        image_mat.rows,
        image_mat.cols,
        CV_8UC1,
        tensor_data + i * shape[EA_H] * pitch,
        pitch);
    }

    if (swap_rb && image_mat.channels() == 3) {
      std::swap(channels[0], channels[2]);
    }

    cv::split(image_mat, channels);
  } while (0);

  return rval;
}

int ResNet50Node::copyTensorToImage(
  ea_tensor_t * tensor, sensor_msgs::msg::Image::SharedPtr & image)
{
  int rval = 0;
  const size_t * shape = ea_tensor_shape(tensor);
  const size_t pitch = ea_tensor_pitch(tensor);
  uint8_t * tensor_data = (uint8_t *)ea_tensor_data_for_read(tensor, EA_CPU);
  void * c1_data = tensor_data;
  void * c2_data = tensor_data + shape[EA_H] * pitch;
  void * c3_data = tensor_data + shape[EA_H] * pitch * 2;
  cv::Mat c1(shape[EA_H], shape[EA_W], CV_8UC1, c1_data, pitch);
  cv::Mat c2(shape[EA_H], shape[EA_W], CV_8UC1, c2_data, pitch);
  cv::Mat c3(shape[EA_H], shape[EA_W], CV_8UC1, c3_data, pitch);
  std::vector<cv::Mat> channels;

  do {
    EA_R_ASSERT(
      image->encoding == "rgb8" || image->encoding == "bgr8" || image->encoding == "mono8");
    EA_R_ASSERT(image->width == shape[EA_W] && image->height == shape[EA_H]);

    image->data.resize(shape[EA_H] * shape[EA_W] * shape[EA_C]);
    image->step = shape[EA_W] * shape[EA_C];
    cv::Mat image_mat = cv::Mat(shape[EA_H], shape[EA_W], CV_8UC3, image->data.data(), image->step);

    if (shape[EA_C] == 1) {
      channels.push_back(c1);
    } else {
      channels.push_back(c1);
      channels.push_back(c2);
      channels.push_back(c3);
    }

    cv::merge(channels, image_mat);
  } while (0);

  return rval;
}

void ResNet50Node::imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  int rval = 0;
  ea_tensor_t * tensor = NULL;
  ea_tensor_t * net_input_tensor = NULL;
  const size_t * input_shape = NULL;
  size_t shape[EA_DIM];
  bool swap_rb = false;

  do {
    EA_R_ASSERT(msg->encoding == "rgb8" || msg->encoding == "bgr8");
    if (msg->encoding == "rgb8") {
      swap_rb = true;
    }

    auto start_time = this->now();

    net_input_tensor = resnet50_input(resnet50_);
    input_shape = ea_tensor_shape(net_input_tensor);
    if (msg->width != input_shape[EA_W] || msg->height != input_shape[EA_H]) {
      shape[EA_N] = 1;
      shape[EA_C] = 3;
      shape[EA_H] = msg->height;
      shape[EA_W] = msg->width;
      tensor = ea_tensor_new(EA_U8, shape, 0);
      EA_R_ASSERT(tensor != NULL);
      EA_R_OK(copyImageToTensor(msg, tensor, swap_rb));
      EA_R_OK(
        ea_crop_resize(&tensor, 1, &net_input_tensor, 1, NULL, EA_TENSOR_COLOR_MODE_RGB, EA_VP));
      ea_tensor_free(tensor);
      tensor = NULL;
    } else {
      EA_R_OK(copyImageToTensor(msg, net_input_tensor, swap_rb));
    }

    auto pre_process_end_time = this->now();
    EA_R_OK(resnet50_vp_forward(resnet50_));
    auto forward_end_time = this->now();

    const resnet50_result_t * resnet50_result = resnet50_arm_post_process(resnet50_);
    EA_R_ASSERT(resnet50_result != NULL);
    auto post_process_end_time = this->now();

    auto stamp = this->now();
    auto classification = vision_msgs::msg::Classification();
    classification.header.stamp = stamp;
    classification.header.frame_id = msg->header.frame_id;

    for (int i = 0; i < resnet50_result->detection_num; ++i) {
      resnet50_det_t & det = resnet50_result->detections[i];

      vision_msgs::msg::ObjectHypothesis object_hypothesis;
      object_hypothesis.class_id = std::string(det.label);
      object_hypothesis.score = det.score;

      classification.results.push_back(object_hypothesis);
    }

    classification_publisher_->publish(classification);

    nlohmann::json performanceJson;
    performanceJson["image_delay_us"] = (start_time - msg->header.stamp).nanoseconds() / 1000;
    performanceJson["preprocess_us"] = (pre_process_end_time - start_time).nanoseconds() / 1000;
    performanceJson["inference_us"] =
      (forward_end_time - pre_process_end_time).nanoseconds() / 1000;
    performanceJson["post_process_us"] =
      (post_process_end_time - forward_end_time).nanoseconds() / 1000;
    performanceJson["cvflow_us"] = resnet50_performance(resnet50_)->cvflow_time_us;
    float fps = ea_calc_fps(&fps_ctx_);
    if (fps > 0.0) {
      last_fps_ = fps;
    }
    performanceJson["fps"] = std::round(double(last_fps_) * 100) / 100;

    auto message = std_msgs::msg::String();
    message.data = performanceJson.dump();
    performance_publisher_->publish(message);
  } while (0);

  if (rval < 0) {
    if (tensor) {
      ea_tensor_free(tensor);
    }
    throw std::runtime_error("RetinafaceNode::ImageCallback() failed.");
  }
}

}  // namespace resnet50
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::resnet50::ResNet50Node)
