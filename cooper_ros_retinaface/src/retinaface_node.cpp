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

#include "cooper_ros_retinaface/retinaface_node.hpp"

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

#define PUB_TOPIC_NAME_DETECTION   "retinaface/detections"
#define PUB_TOPIC_NAME_LANDMARK    "retinaface/landmarks"
#define PUB_TOPIC_NAME_PERFORMANCE "retinaface/performance"
#define SUB_TOPIC_NAME_IMAGE       "image"

#define PARAM_NAME_MODEL_PATH     "model_path"
#define PARAM_NAME_CONF_THRESHOLD "conf_threshold"
#define PARAM_NAME_NMS_THRESHOLD  "nms_threshold"
#define PARAM_NAME_MAX_DET_NUM    "max_det_num"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

namespace cooper_ros
{
namespace retinaface
{
RetinafaceNode::RetinafaceNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("RetinafaceNode", options)
{
  initNetwork();

  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  image_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
    SUB_TOPIC_NAME_IMAGE,
    reliable_qos,
    std::bind(&RetinafaceNode::imageCallback, this, std::placeholders::_1));

  detection_publisher_ = this->create_publisher<vision_msgs::msg::Detection2DArray>(
    PUB_TOPIC_NAME_DETECTION,
    reliable_qos);
  landmark_publisher_ = this->create_publisher<cooper_ros_msgs::msg::LandmarkDetection2DArray>(
    PUB_TOPIC_NAME_LANDMARK,
    reliable_qos);
  performance_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_PERFORMANCE, reliable_qos);

  memset(&fps_ctx_, 0, sizeof(ea_calc_fps_ctx_t));
  fps_ctx_.count_period = 0;  // will calculate fps every 1 second
  last_fps_ = 0.0;
}

RetinafaceNode::~RetinafaceNode()
{
  if (retinaface_) {
    retinaface_free(retinaface_);
    retinaface_ = NULL;
  }
}

void RetinafaceNode::initNetwork()
{
  int rval = 0;

  do {
    std::string model_path = declareString(PARAM_NAME_MODEL_PATH);
    float conf_threshold = this->declare_parameter(PARAM_NAME_CONF_THRESHOLD, 0.8);
    float nms_threshold = this->declare_parameter(PARAM_NAME_NMS_THRESHOLD, 0.4);
    int max_det_num = this->declare_parameter(PARAM_NAME_MAX_DET_NUM, 300);

    retinaface_params_t net_params;
    memset(&net_params, 0, sizeof(retinaface_params_t));
    net_params.log_level = EA_LOG_LEVEL_NOTICE;
    net_params.model_path = model_path.c_str();
    net_params.conf_threshold = conf_threshold;
    net_params.nms_threshold = nms_threshold;
    net_params.max_det_num = max_det_num;
    retinaface_ = retinaface_new(&net_params);
    EA_R_OK(retinaface_ != NULL);
  } while (0);

  if (rval < 0) {
    throw std::runtime_error("RetinafaceNode::Init() failed.");
  }
}

int RetinafaceNode::copyImageToTensor(
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
    EA_R_ASSERT(image_mat.channels() == (int)shape[EA_C]);
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

int RetinafaceNode::copyTensorToImage(
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

void RetinafaceNode::imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  int rval = 0;
  ea_tensor_t * tensor = NULL;
  ea_tensor_t * net_input_tensor = NULL;
  const size_t * input_shape = NULL;
  size_t shape[EA_DIM];
  bool swap_rb = false;
  const retinaface_result_t * result = NULL;

  do {
    EA_R_ASSERT(msg->encoding == "rgb8" || msg->encoding == "bgr8");
    if (msg->encoding == "bgr8") {
      swap_rb = true;
    }

    auto start_time = this->now();

    net_input_tensor = retinaface_input(retinaface_);
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
    EA_R_OK(retinaface_vp_forward(retinaface_));
    auto forward_end_time = this->now();

    result = retinaface_arm_post_process(retinaface_);
    EA_R_OK(result != NULL);
    auto post_process_end_time = this->now();

    auto stamp = this->now();
    auto detection_array = vision_msgs::msg::Detection2DArray();
    detection_array.header.stamp = stamp;
    detection_array.header.frame_id = msg->header.frame_id;
    auto landmark_array = cooper_ros_msgs::msg::LandmarkDetection2DArray();
    landmark_array.header.stamp = stamp;
    landmark_array.header.frame_id = msg->header.frame_id;

    for (int i = 0; i < result->detection_num; ++i) {
      retinaface_det_t & det = result->detections[i];

      vision_msgs::msg::Detection2D face_detection;
      face_detection.header.stamp = stamp;
      face_detection.header.frame_id = msg->header.frame_id;
      face_detection.results.resize(1);
      face_detection.results[0].hypothesis.class_id = "face";
      face_detection.results[0].hypothesis.score = det.score;
      face_detection.id = std::to_string(i);
      face_detection.bbox.size_x = det.x_end - det.x_start;
      face_detection.bbox.size_y = det.y_end - det.y_start;
      face_detection.bbox.center.position.x = det.x_start + face_detection.bbox.size_x / 2;
      face_detection.bbox.center.position.y = det.y_start + face_detection.bbox.size_y / 2;
      detection_array.detections.push_back(face_detection);

      cooper_ros_msgs::msg::LandmarkDetection2D landmarks;
      landmarks.header.stamp = stamp;
      landmarks.header.frame_id = msg->header.frame_id;
      landmarks.results.resize(1);
      landmarks.results[0].hypothesis.class_id = "landmark";
      landmarks.results[0].hypothesis.score = det.score;
      landmarks.id = std::to_string(i);
      landmarks.points.resize(5);
      for (int j = 0; j < 5; ++j) {
        landmarks.points[j].x = det.landmarks[j][0];
        landmarks.points[j].y = det.landmarks[j][1];
      }
      landmark_array.detections.push_back(landmarks);
    }

    detection_publisher_->publish(detection_array);
    landmark_publisher_->publish(landmark_array);

    nlohmann::json performanceJson;
    performanceJson["image_delay_us"] = (start_time - msg->header.stamp).nanoseconds() / 1000;
    performanceJson["preprocess_us"] = (pre_process_end_time - start_time).nanoseconds() / 1000;
    performanceJson["inference_us"] =
      (forward_end_time - pre_process_end_time).nanoseconds() / 1000;
    performanceJson["post_process_us"] =
      (post_process_end_time - forward_end_time).nanoseconds() / 1000;
    performanceJson["cvflow_us"] = retinaface_performance(retinaface_)->cvflow_time_us;
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

}  // namespace retinaface
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::retinaface::RetinafaceNode)
