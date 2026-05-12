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

#include "cooper_ros_iav_canvas/iav_canvas_node.hpp"

#include <iostream>
#include <chrono>
#include <utility>
#include <mutex>
#include <thread>

#include <unistd.h>
#include <sys/ioctl.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace std::chrono_literals;

#define PUB_TOPIC_NAME_NV12 "iav_canvas/nv12"
#define PUB_TOPIC_NAME_RGB  "iav_canvas/rgb8"
#define PUB_TOPIC_NAME_BGR  "iav_canvas/bgr8"
#define PUB_TOPIC_NAME_MONO "iav_canvas/mono8"

#define PARAM_NAME_CANVAS_ID    "canvas_id"
#define PARAM_NAME_FPS          "fps"
#define PARAM_NAME_ENABLE_NV12  "enable_nv12"
#define PARAM_NAME_ENABLE_RGB8  "enable_rgb8"
#define PARAM_NAME_ENABLE_BGR8  "enable_bgr8"
#define PARAM_NAME_ENABLE_MONO8 "enable_mono8"
#define PARAM_NAME_WIDTH_RGB8   "width_rgb8"
#define PARAM_NAME_HEIGHT_RGB8  "height_rgb8"
#define PARAM_NAME_WIDTH_BGR8   "width_bgr8"
#define PARAM_NAME_HEIGHT_BGR8  "height_bgr8"
#define PARAM_NAME_WIDTH_MONO8  "width_mono8"
#define PARAM_NAME_HEIGHT_MONO8 "height_mono8"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

namespace cooper_ros
{
namespace iav_canvas
{

IavCanvasNode::IavCanvasNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("IavCanvasNode", options)
{
  rcl_interfaces::msg::ParameterDescriptor param_descriptor;

  param_descriptor.description = "Canvas ID. Default: 1";
  old_canvas_id_ = canvas_id_ = this->declare_parameter(PARAM_NAME_CANVAS_ID, 1, param_descriptor);
  if (canvas_id_ < 0) {
    RCLCPP_ERROR(this->get_logger(), "canvas_id [%d] must be no smaller than 0", canvas_id_);
    throw std::runtime_error("canvas_id must be no smaller than 0");
  }

  img_resource_ = ea_img_resource_new(EA_CANVAS, (void *)(unsigned long)canvas_id_);
  if (img_resource_ == NULL) {
    RCLCPP_ERROR(this->get_logger(), "failed to create img resource, canvas_id: %d", canvas_id_);
    throw std::runtime_error("Failed to create img resource");
  }

  if (getCanvasSize() < 0) {
    throw std::runtime_error("Failed to get canvas: " + std::to_string(canvas_id_) + " size");
  }
  width_bgr8_ = width_rgb8_ = canvas_width_;
  height_bgr8_ = height_rgb8_ = canvas_height_;

  param_descriptor.description = "Frames per second for publishing the images. Default: 30";
  old_fps_ = fps_ = this->declare_parameter(PARAM_NAME_FPS, 30.0, param_descriptor);
  if (fps_ <= 0.0) {
    RCLCPP_ERROR(this->get_logger(), "fps [%lf] must be greater than 0", fps_);
    throw std::runtime_error("fps must be greater than 0");
  }

  param_descriptor.description = "Enable the topic of the NV12 image data. Default: false";
  enable_nv12_ = this->declare_parameter(PARAM_NAME_ENABLE_NV12, false, param_descriptor);

  param_descriptor.description = "Enable the topic of the RGB8 image data. Default: false";
  enable_rgb8_ = this->declare_parameter(PARAM_NAME_ENABLE_RGB8, false, param_descriptor);

  param_descriptor.description = "Enable the topic of the BGR8 image data. Default: false";
  enable_bgr8_ = this->declare_parameter(PARAM_NAME_ENABLE_BGR8, false, param_descriptor);

  param_descriptor.description = "Enable the topic of the MONO8 image data. Default: false";
  enable_mono8_ = this->declare_parameter(PARAM_NAME_ENABLE_MONO8, false, param_descriptor);

  param_descriptor.description = "Width of the RGB image data. Default: Width of the NV12 image";
  width_rgb8_ = this->declare_parameter(PARAM_NAME_WIDTH_RGB8, width_rgb8_, param_descriptor);
  if (width_rgb8_ <= 0) {
    width_rgb8_ = canvas_width_;
  }

  param_descriptor.description = "Height of the RGB image data. Default: Height of the NV12 image";
  height_rgb8_ = this->declare_parameter(PARAM_NAME_HEIGHT_RGB8, height_rgb8_, param_descriptor);
  if (height_rgb8_ <= 0) {
    height_rgb8_ = canvas_height_;
  }

  param_descriptor.description = "Width of the BGR image data. Default: Width of the NV12 image";
  width_bgr8_ = this->declare_parameter(PARAM_NAME_WIDTH_BGR8, width_bgr8_, param_descriptor);
  if (width_bgr8_ <= 0) {
    width_bgr8_ = canvas_width_;
  }

  param_descriptor.description = "Height of the BGR image data. Default: Height of the NV12 image";
  height_bgr8_ = this->declare_parameter(PARAM_NAME_HEIGHT_BGR8, height_bgr8_, param_descriptor);
  if (height_bgr8_ <= 0) {
    height_bgr8_ = canvas_height_;
  }

  param_descriptor.description = "Width of the MONO image data. Default: Width of the NV12 image";
  width_mono8_ = this->declare_parameter(PARAM_NAME_WIDTH_MONO8, width_mono8_, param_descriptor);
  if (width_mono8_ <= 0) {
    width_mono8_ = canvas_width_;
  }

  param_descriptor.description = "Height of the MONO image data. Default: Height of the NV12 image";
  height_mono8_ = this->declare_parameter(PARAM_NAME_HEIGHT_MONO8, height_mono8_, param_descriptor);
  if (height_mono8_ <= 0) {
    height_mono8_ = canvas_height_;
  }

  this->updatePublishers();

  param_callback_handle_ = this->add_on_set_parameters_callback(
    std::bind(&IavCanvasNode::onSetParameters, this, std::placeholders::_1));

  timer_ = this->create_wall_timer(
    std::chrono::microseconds((long)(1000000 / fps_)),
    std::bind(&IavCanvasNode::publishImgNoWait, this));
}

IavCanvasNode::~IavCanvasNode()
{
  if (img_resource_) {
    ea_img_resource_free(img_resource_);
    img_resource_ = NULL;
  }
}

void IavCanvasNode::updatePublishers()
{
  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  if (enable_nv12_) {
    if (nv12_publisher_ == nullptr) {
      nv12_publisher_ =
        this->create_publisher<sensor_msgs::msg::Image>(PUB_TOPIC_NAME_NV12, reliable_qos);
    }
  } else {
    nv12_publisher_ = nullptr;
  }

  if (enable_rgb8_) {
    if (rgb_publisher_ == nullptr) {
      rgb_publisher_ =
        this->create_publisher<sensor_msgs::msg::Image>(PUB_TOPIC_NAME_RGB, reliable_qos);
    }
  } else {
    rgb_publisher_ = nullptr;
  }

  if (enable_bgr8_) {
    if (bgr_publisher_ == nullptr) {
      bgr_publisher_ =
        this->create_publisher<sensor_msgs::msg::Image>(PUB_TOPIC_NAME_BGR, reliable_qos);
    }
  } else {
    bgr_publisher_ = nullptr;
  }

  if (enable_mono8_) {
    if (mono_publisher_ == nullptr) {
      mono_publisher_ =
        this->create_publisher<sensor_msgs::msg::Image>(PUB_TOPIC_NAME_MONO, reliable_qos);
    }
  } else {
    mono_publisher_ = nullptr;
  }
}

int IavCanvasNode::getCanvasSize()
{
  int iav_fd = ea_env_fd_iav();
  struct ::iav_canvas_cfg canvas_cfg;

  if (iav_fd < 0) {
    throw std::runtime_error("Failed to get iav fd.");
  }

  memset(&canvas_cfg, 0, sizeof(struct iav_canvas_cfg));
  canvas_cfg.canvas_id = canvas_id_;
  if (ioctl(iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
    throw std::runtime_error("Failed to get the config of canvas: " + std::to_string(canvas_id_));
  }

  canvas_width_ = canvas_cfg.max.width;
  canvas_height_ = canvas_cfg.max.height;

  return 0;
}

rcl_interfaces::msg::SetParametersResult IavCanvasNode::onSetParameters(
  const std::vector<rclcpp::Parameter> & parameters)
{
  ea_img_resource_t * new_img_resource = NULL;
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  int tmp = 0;
  double tmp_double = 0.0;

  std::lock_guard<std::mutex> lock(param_mutex_);

  for (const auto & param : parameters) {
    if (param.get_name() == PARAM_NAME_CANVAS_ID) {
      tmp = param.as_int();
      if (tmp < 0) {
        result.successful = false;
        result.reason = "canvas_id must be no smaller than 0";
        break;
      }
      canvas_id_ = tmp;
    } else if (param.get_name() == PARAM_NAME_FPS) {
      tmp_double = param.as_double();
      if (tmp_double <= 0) {
        result.successful = false;
        result.reason = "fps must be greater than 0";
        break;
      }
      fps_ = tmp_double;
    } else if (param.get_name() == PARAM_NAME_ENABLE_NV12) {
      enable_nv12_ = param.as_bool();
      RCLCPP_DEBUG(this->get_logger(), "enable_nv12 changed to %d\n", enable_nv12_);
    } else if (param.get_name() == PARAM_NAME_ENABLE_RGB8) {
      enable_rgb8_ = param.as_bool();
      RCLCPP_DEBUG(this->get_logger(), "enable_rgb8 changed to %d\n", enable_rgb8_);
    } else if (param.get_name() == PARAM_NAME_ENABLE_BGR8) {
      enable_bgr8_ = param.as_bool();
      RCLCPP_DEBUG(this->get_logger(), "enable_bgr8 changed to %d\n", enable_bgr8_);
    } else if (param.get_name() == PARAM_NAME_ENABLE_MONO8) {
      enable_mono8_ = param.as_bool();
      RCLCPP_DEBUG(this->get_logger(), "enable_mono8 changed to %d\n", enable_mono8_);
    } else if (param.get_name() == PARAM_NAME_WIDTH_RGB8) {
      width_rgb8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "width_rgb8 changed to %d\n", width_rgb8_);
    } else if (param.get_name() == PARAM_NAME_HEIGHT_RGB8) {
      height_rgb8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "height_rgb8 changed to %d\n", height_rgb8_);
    } else if (param.get_name() == PARAM_NAME_WIDTH_BGR8) {
      width_bgr8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "width_bgr8 changed to %d\n", width_bgr8_);
    } else if (param.get_name() == PARAM_NAME_HEIGHT_BGR8) {
      height_bgr8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "height_bgr8 changed to %d\n", height_bgr8_);
    } else if (param.get_name() == PARAM_NAME_WIDTH_MONO8) {
      width_mono8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "width_mono8 changed to %d\n", width_mono8_);
    } else if (param.get_name() == PARAM_NAME_HEIGHT_MONO8) {
      height_mono8_ = param.as_int();
      RCLCPP_DEBUG(this->get_logger(), "height_mono8 changed to %d\n", height_mono8_);
    }
  }

  if (old_canvas_id_ != canvas_id_) {
    do {
      new_img_resource = ea_img_resource_new(EA_CANVAS, (void *)(unsigned long)canvas_id_);
      if (new_img_resource == NULL) {
        result.successful = false;
        result.reason = "failed to create img resource, canvas_id: " + std::to_string(canvas_id_);
        canvas_id_ = old_canvas_id_;
        break;
      }
      if (getCanvasSize() < 0) {
        result.successful = false;
        result.reason = "failed to get canvas size, canvas_id: " + std::to_string(canvas_id_);
        canvas_id_ = old_canvas_id_;
        break;
      }
      if (img_resource_) {
        ea_img_resource_free(img_resource_);
        img_resource_ = NULL;
      }
      img_resource_ = new_img_resource;
      old_canvas_id_ = canvas_id_;
      RCLCPP_DEBUG(this->get_logger(), "canvas_id changed to %d\n", canvas_id_);
    } while (0);
  }

  if (fps_ != old_fps_) {
    timer_->cancel();
    timer_ = this->create_wall_timer(
      std::chrono::microseconds((long)(1000000 / fps_)),
      std::bind(&IavCanvasNode::publishImgNoWait, this));
    old_fps_ = fps_;
    RCLCPP_DEBUG(this->get_logger(), "fps changed to %lf\n", fps_);
  }

  if (width_rgb8_ <= 0 || height_rgb8_ <= 0) {
    RCLCPP_WARN(
      this->get_logger(),
      "rgb size %dx%d is illegal, use default %dx%d.\n",
      width_rgb8_,
      height_rgb8_,
      canvas_width_,
      canvas_height_);
    width_rgb8_ = canvas_width_;
    height_rgb8_ = canvas_height_;
  }
  if (width_bgr8_ <= 0 || height_bgr8_ <= 0) {
    RCLCPP_WARN(
      this->get_logger(),
      "bgr size %dx%d is illegal, use default %dx%d.\n",
      width_bgr8_,
      height_bgr8_,
      canvas_width_,
      canvas_height_);
    width_bgr8_ = canvas_width_;
    height_bgr8_ = canvas_height_;
  }
  if (width_mono8_ <= 0 || height_mono8_ <= 0) {
    RCLCPP_WARN(
      this->get_logger(),
      "mono size %dx%d is illegal, use default %dx%d.\n",
      width_mono8_,
      height_mono8_,
      canvas_width_,
      canvas_height_);
    width_mono8_ = canvas_width_;
    height_mono8_ = canvas_height_;
  }

  this->updatePublishers();

  return result;
}

int IavCanvasNode::copyTensorToImage(
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
  cv::Mat image_mat;

  do {
    EA_R_ASSERT(
      image->encoding == "rgb8" || image->encoding == "bgr8" || image->encoding == "mono8");
    EA_R_ASSERT(image->width == shape[EA_W] && image->height == shape[EA_H]);

    image->data.resize(shape[EA_H] * shape[EA_W] * shape[EA_C]);
    image->step = shape[EA_W] * shape[EA_C];
    if (image->encoding == "mono8") {
      image_mat = cv::Mat(shape[EA_H], shape[EA_W], CV_8UC1, image->data.data(), image->step);
    } else {
      image_mat = cv::Mat(shape[EA_H], shape[EA_W], CV_8UC3, image->data.data(), image->step);
    }

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

void IavCanvasNode::publishNv12(ea_tensor_t * img_data_tensor)
{
  auto img_msg = std::make_shared<sensor_msgs::msg::Image>();

  img_msg->header.stamp = now();
  img_msg->header.frame_id = "canvas" + std::to_string(canvas_id_);
  img_msg->width = ea_tensor_shape(img_data_tensor)[EA_W];
  img_msg->height = ea_tensor_shape(img_data_tensor)[EA_H];
  img_msg->encoding = "nv12";
  img_msg->step = ea_tensor_pitch(img_data_tensor);
  img_msg->data.resize(img_msg->step * img_msg->height * 3 / 2);
  memcpy(
    img_msg->data.data(),
    ea_tensor_data(img_data_tensor),
    img_msg->step * img_msg->height * 3 / 2);

  nv12_publisher_->publish(*img_msg);
}

void IavCanvasNode::publishRgb(ea_tensor_t * img_data_tensor)
{
  int rval = EA_SUCCESS;
  auto img_msg = std::make_shared<sensor_msgs::msg::Image>();
  ea_tensor_t * rgb_tensor = NULL;
  size_t shape[EA_DIM] = {0};

  do {
    shape[EA_N] = 1;
    shape[EA_C] = 3;
    shape[EA_H] = height_rgb8_;
    shape[EA_W] = width_rgb8_;
    rgb_tensor = ea_tensor_new(EA_U8, shape, 0);
    RVAL_ASSERT(rgb_tensor != NULL);
    RVAL_OK(ea_cvt_color_resize(img_data_tensor, rgb_tensor, EA_COLOR_YUV2RGB_NV12, EA_VP));

    img_msg->header.stamp = now();
    img_msg->header.frame_id = "canvas" + std::to_string(canvas_id_);
    img_msg->width = ea_tensor_shape(rgb_tensor)[EA_W];
    img_msg->height = ea_tensor_shape(rgb_tensor)[EA_H];
    img_msg->encoding = "rgb8";
    copyTensorToImage(rgb_tensor, img_msg);

    rgb_publisher_->publish(*img_msg);
  } while (0);

  if (rgb_tensor) {
    ea_tensor_free(rgb_tensor);
    rgb_tensor = NULL;
  }
}

void IavCanvasNode::publishBgr(ea_tensor_t * img_data_tensor)
{
  int rval = EA_SUCCESS;
  auto img_msg = std::make_shared<sensor_msgs::msg::Image>();
  ea_tensor_t * bgr_tensor = NULL;
  size_t shape[EA_DIM] = {0};

  do {
    shape[EA_N] = 1;
    shape[EA_C] = 3;
    shape[EA_H] = height_bgr8_;
    shape[EA_W] = width_bgr8_;
    bgr_tensor = ea_tensor_new(EA_U8, shape, 0);
    RVAL_ASSERT(bgr_tensor != NULL);
    RVAL_OK(ea_cvt_color_resize(img_data_tensor, bgr_tensor, EA_COLOR_YUV2BGR_NV12, EA_VP));

    img_msg->header.stamp = now();
    img_msg->header.frame_id = "canvas" + std::to_string(canvas_id_);
    img_msg->width = ea_tensor_shape(bgr_tensor)[EA_W];
    img_msg->height = ea_tensor_shape(bgr_tensor)[EA_H];
    img_msg->encoding = "bgr8";
    copyTensorToImage(bgr_tensor, img_msg);

    bgr_publisher_->publish(*img_msg);
  } while (0);

  if (bgr_tensor) {
    ea_tensor_free(bgr_tensor);
    bgr_tensor = NULL;
  }
}

void IavCanvasNode::publishMono(ea_tensor_t * img_data_tensor)
{
  int rval = EA_SUCCESS;
  auto img_msg = std::make_shared<sensor_msgs::msg::Image>();
  ea_tensor_t * mono_tensor = NULL;
  size_t shape[EA_DIM] = {0};

  do {
    shape[EA_N] = 1;
    shape[EA_C] = 1;
    shape[EA_H] = height_mono8_;
    shape[EA_W] = width_mono8_;
    mono_tensor = ea_tensor_new(EA_U8, shape, 0);
    RVAL_ASSERT(mono_tensor != NULL);
    RVAL_OK(
      ea_crop_resize(&img_data_tensor, 1, &mono_tensor, 1, NULL, EA_TENSOR_COLOR_MODE_GRAY, EA_VP));

    img_msg->header.stamp = now();
    img_msg->header.frame_id = "canvas" + std::to_string(canvas_id_);
    img_msg->width = ea_tensor_shape(mono_tensor)[EA_W];
    img_msg->height = ea_tensor_shape(mono_tensor)[EA_H];
    img_msg->encoding = "mono8";
    copyTensorToImage(mono_tensor, img_msg);

    mono_publisher_->publish(*img_msg);
  } while (0);

  if (mono_tensor) {
    ea_tensor_free(mono_tensor);
    mono_tensor = NULL;
  }
}

void IavCanvasNode::publishImgNoWait()
{
  int rval = EA_SUCCESS;
  ea_img_resource_data_t data;
  ea_tensor_t * img_data_tensor = NULL;

  if (!enable_nv12_ && !enable_rgb8_ && !enable_bgr8_ && !enable_mono8_) {
    return;
  }

  memset(&data, 0, sizeof(ea_img_resource_data_t));
  std::lock_guard<std::mutex> lock(param_mutex_);

  rval = ea_img_resource_hold_data_nowait(img_resource_, &data);
  if (rval != EA_SUCCESS) {
    RCLCPP_ERROR(
      this->get_logger(),
      "[%s,%d]failed to hold data, rval: %d\n",
      __func__,
      __LINE__,
      rval);
    return;
  }

  img_data_tensor = EA_CANVAS_YUV_TENSOR(data.tensor_group);

  //publish nv12 topic
  if (enable_nv12_ && nv12_publisher_ != nullptr) {
    publishNv12(img_data_tensor);
  }

  //transform nv12 to rgb, and public rgb topic
  if (enable_rgb8_ && rgb_publisher_ != nullptr) {
    publishRgb(img_data_tensor);
  }

  //transform nv12 to bgr, and public bgr topic
  if (enable_bgr8_ && bgr_publisher_ != nullptr) {
    publishBgr(img_data_tensor);
  }

  //transform nv12 to bgr, and public bgr topic
  if (enable_mono8_ && mono_publisher_ != nullptr) {
    publishMono(img_data_tensor);
  }

  ea_img_resource_drop_data(img_resource_, &data);
}

}  // namespace iav_canvas
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::iav_canvas::IavCanvasNode)
