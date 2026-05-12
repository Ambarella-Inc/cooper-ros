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

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp/exceptions.hpp>

#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>

#ifdef USE_CV_BRIDGE_H
#include <cv_bridge/cv_bridge.h>
#else
#include <cv_bridge/cv_bridge.hpp>
#endif

#define SUB_TOPIC_NAME_DETECTION      "detections"
#define SUB_TOPIC_NAME_CLASSIFICATION "classification"
#define SUB_TOPIC_NAME_SEGMENTATION   "segmentation"
#define SUB_TOPIC_NAME_LANDMARK       "landmarks"
#define SUB_TOPIC_NAME_EFM_IMAGE      "efm_image"

#define PARAM_NAME_OSD                   "media"
#define PARAM_NAME_OVERLAY_BUFFER_OFFSET "overlay_buffer_offset"
#define PARAM_NAME_ENABLE_BBOX_TEXTBOX   "enable_bbox_textbox"
#define PARAM_NAME_ENABLE_SEGMENTATION   "enable_segmentation"
#define PARAM_NAME_ENABLE_EFM            "enable_efm"
#define PARAM_NAME_ENABLE_CROP_TO_SQUARE "enable_crop_to_square"
#define PARAM_NAME_HAS_LANDMARK          "has_landmark"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

namespace cooper_ros
{
namespace iav_osd
{

IavOsdNode::IavOsdNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("IavOsdNode", options)
{
  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  bool enable_efm = this->declare_parameter(PARAM_NAME_ENABLE_EFM, false);
  if (enable_efm) {
    efm_image_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
      SUB_TOPIC_NAME_EFM_IMAGE,
      reliable_qos,
      std::bind(&IavOsdNode::efmImageCallback, this, std::placeholders::_1));
  }

  bool has_landmark = this->declare_parameter(PARAM_NAME_HAS_LANDMARK, false);
  if (has_landmark) {
    detection_subscriber_ =
      std::make_shared<message_filters::Subscriber<vision_msgs::msg::Detection2DArray>>(
        this,
        std::string(SUB_TOPIC_NAME_DETECTION),
        rmw_qos_profile_default);
    landmark_subscriber_ =
      std::make_shared<message_filters::Subscriber<cooper_ros_msgs::msg::LandmarkDetection2DArray>>(
        this,
        std::string(SUB_TOPIC_NAME_LANDMARK),
        rmw_qos_profile_default);
    exact_time_synchronizer_ = std::make_shared<message_filters::Synchronizer<ExactTimePolicy>>(
      ExactTimePolicy(10),
      *detection_subscriber_,
      *landmark_subscriber_);
    exact_time_synchronizer_->registerCallback(std::bind(
      &IavOsdNode::faceDetectionCallback,
      this,
      std::placeholders::_1,
      std::placeholders::_2));
  } else {
    detection_subscription_ = this->create_subscription<vision_msgs::msg::Detection2DArray>(
      SUB_TOPIC_NAME_DETECTION,
      reliable_qos,
      std::bind(&IavOsdNode::detectionCallback, this, std::placeholders::_1));
  }

  classification_subscription_ = this->create_subscription<vision_msgs::msg::Classification>(
    SUB_TOPIC_NAME_CLASSIFICATION,
    reliable_qos,
    std::bind(&IavOsdNode::classificationCallback, this, std::placeholders::_1));

  enable_segmentation_ = this->declare_parameter(PARAM_NAME_ENABLE_SEGMENTATION, false);
  if (enable_segmentation_) {
    segmentation_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
      SUB_TOPIC_NAME_SEGMENTATION,
      reliable_qos,
      std::bind(&IavOsdNode::segmentationCallback, this, std::placeholders::_1));
  }
  segmentation_tensor_ = NULL;

  initIavOsd();

  timer_fps_ = ea_display_obj_params(display_)->stream_fps;
  if (timer_fps_ <= 0) {
    timer_fps_ = 30;
  }
  timer_ = this->create_wall_timer(
    std::chrono::microseconds(1000000 / timer_fps_),
    std::bind(&IavOsdNode::timerCallback, this));

  EA_LOG_NOTICE("IavOsdNode initialized\n");
}

IavOsdNode::~IavOsdNode()
{
  if (segmentation_tensor_ != NULL) {
    ea_tensor_free(segmentation_tensor_);
    segmentation_tensor_ = NULL;
  }

  if (io_efm_) {
    ea_io_deinit_efm(io_efm_);
    io_efm_ = NULL;
  }

  if (display_) {
    ea_display_free(display_);
    display_ = NULL;
  }
}

void IavOsdNode::initIavOsd()
{
  int rval = 0;
  ea_display_params_t display_params;
  int display_features = 0;

  do {
    bool enable_bbox_textbox = this->declare_parameter(PARAM_NAME_ENABLE_BBOX_TEXTBOX, true);
    if (enable_bbox_textbox) {
      display_features |= EA_DISPLAY_BBOX_TEXTBOX;
    }

    if (enable_segmentation_) {
      display_features |= EA_DISPLAY_256_COLORS;
    }

    std::string osd = this->declare_parameter(PARAM_NAME_OSD, "stream:0");
    // osd should be "hdmi" or in the format of "stream:ID", ID is the stream ID, starts from 0
    if (osd == "hdmi") {
      osd_type_ = "hdmi";
      display_ = ea_display_new(EA_DISPLAY_VOUT, EA_DISPLAY_ANALOG_VOUT, display_features, NULL);
    } else if (osd.find("stream:") == 0) {
      osd_type_ = "stream";
      stream_id_ = std::stoi(osd.substr(7));

      memset(&display_params, 0, sizeof(ea_display_params_t));
      display_params.overlay_buffer_offset =
        this->declare_parameter(PARAM_NAME_OVERLAY_BUFFER_OFFSET, 0);
      EA_LOG_NOTICE(
        "display_params.overlay_buffer_offset: %ld\n",
        display_params.overlay_buffer_offset);
      display_ = ea_display_new(EA_DISPLAY_STREAM, stream_id_, display_features, &display_params);
    } else {
      throw rclcpp::exceptions::InvalidParameterValueException(
        "parameter osd must be 'hdmi' or in the format of 'stream:ID', ID is the stream ID, starts "
        "from 0. got: " +
        osd);
    }

    EA_R_ASSERT(display_ != NULL);

    bool enable_efm = this->get_parameter(PARAM_NAME_ENABLE_EFM).as_bool();
    if (enable_efm) {
      EA_R_ASSERT(osd_type_ == "stream");
      ea_io_efm_params_t efm_params;
      memset(&efm_params, 0, sizeof(ea_io_efm_params_t));
      efm_params.bg_path = NULL;
      efm_params.display = display_;
      efm_params.hold_frame = 1;
      efm_params.clip_frame_max_num = 1;
      efm_params.device = EA_VP;
      io_efm_ = ea_io_init_efm(&efm_params);
      EA_R_ASSERT(io_efm_ != NULL);
      this->declare_parameter(PARAM_NAME_ENABLE_CROP_TO_SQUARE, false);
    }
  } while (0);

  if (enable_segmentation_) {
    init256ColorTable();
  }

  if (rval < 0) {
    throw std::runtime_error("IavOsdNode::initIavOsd() failed.");
  }

  EA_LOG_NOTICE(
    "IAV_OSD initialized, osd_type: %s, stream_id: %d\n",
    osd_type_.c_str(),
    stream_id_);
}

void IavOsdNode::init256ColorTable()
{
#define SEGMENTATION_MAP_TRANSPARENCY (200)
  int rval = 0;
  int i = 0, shift = 0;
  int ind[256];
  ea_display_rgba_t colors_table[256];

  memset(colors_table, 0, sizeof(ea_display_rgba_t) * 256);
  for (i = 0; i < 256; i++) {
    ind[i] = i;
  }
  for (shift = 7; shift >= 0; shift--) {
    for (i = 0; i < 256; i++) {
      colors_table[i].r |= ((ind[i] >> 0) & 1) << shift;
      colors_table[i].g |= ((ind[i] >> 1) & 1) << shift;
      colors_table[i].b |= ((ind[i] >> 2) & 1) << shift;
      colors_table[i].a = SEGMENTATION_MAP_TRANSPARENCY;
    }

    for (i = 0; i < 256; i++) {
      ind[i] >>= 3;
    }
  }

  do {
    EA_R_OK(ea_display_set_256_colors_table(display_, colors_table, 256));
    EA_LOG_NOTICE("loaded 256 colors\n");
  } while (0);
}

int IavOsdNode::copyImageToTensor(
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

void IavOsdNode::efmImageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  int rval = 0;
  ea_tensor_t * tensor = NULL;
  size_t shape[EA_DIM];
  ea_io_image_preprocess_t preprocess;

  do {
    EA_R_ASSERT(io_efm_ != NULL);
    EA_R_ASSERT(msg->encoding == "rgb8" || msg->encoding == "bgr8" || msg->encoding == "mono8");

    memset(&preprocess, 0, sizeof(ea_io_image_preprocess_t));
    preprocess.crop_to_square = this->get_parameter(PARAM_NAME_ENABLE_CROP_TO_SQUARE).as_bool();
    preprocess.device = EA_CPU;
    preprocess.dst_color = EA_TENSOR_COLOR_MODE_YUV_NV12;
    if (msg->encoding == "rgb8") {
      shape[EA_C] = 3;
      preprocess.src_color = EA_TENSOR_COLOR_MODE_RGB;
    } else if (msg->encoding == "bgr8") {
      shape[EA_C] = 3;
      preprocess.src_color = EA_TENSOR_COLOR_MODE_BGR;
    } else if (msg->encoding == "mono8") {
      shape[EA_C] = 1;
      preprocess.src_color = EA_TENSOR_COLOR_MODE_GRAY;
    } else {
      RCLCPP_ERROR(this->get_logger(), "Unsupported image encoding: %s", msg->encoding.c_str());
      rval = -1;
      break;
    }

    shape[EA_N] = 1;
    shape[EA_H] = msg->height;
    shape[EA_W] = msg->width;
    tensor = ea_tensor_new(EA_U8, shape, 0);
    EA_R_ASSERT(tensor != NULL);
    EA_R_OK(copyImageToTensor(msg, tensor, false));
    EA_R_OK(ea_io_set_efm_image(io_efm_, tensor, &preprocess));
    ea_tensor_free(tensor);
    tensor = NULL;
  } while (0);

  if (rval < 0) {
    if (tensor) {
      ea_tensor_free(tensor);
    }

    throw std::runtime_error("IavOsdNode::efmImageCallback() failed.");
  }
}

void IavOsdNode::drawDetectionResult(const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg)
{
  int rval = 0;
  char text[256];
  int ea_16_color = EA_16_COLORS_WHITE;
  int bbox_thickness = 4;

  do {
    ea_display_obj_params(display_)->obj_win_w = 1.0;
    ea_display_obj_params(display_)->obj_win_h = 1.0;
    ea_display_obj_params(display_)->border_thickness = bbox_thickness;
    ea_display_obj_params(display_)->font_size = 32;
    ea_display_obj_params(display_)->text_color = EA_16_COLORS_WHITE;

    for (size_t i = 0; i < msg->detections.size(); ++i) {
      snprintf(
        text,
        256,
        "%.3f %s",
        msg->detections[i].results[0].hypothesis.score,
        msg->detections[i].results[0].hypothesis.class_id.c_str());

      try {
        ea_16_color = std::stoi(msg->detections[i].id) % EA_16_COLORS_MAX_NUM;
      } catch (const std::invalid_argument & e) {
        ea_16_color = i % EA_16_COLORS_MAX_NUM;
      }

      ea_display_obj_params(display_)->box_color = (ea_16_colors_t)ea_16_color;
      ea_display_obj_params(display_)->text_color = (ea_16_colors_t)ea_16_color;
      ea_display_set_bbox(
        display_,
        text,
        msg->detections[i].bbox.center.position.x - msg->detections[i].bbox.size_x / 2,
        msg->detections[i].bbox.center.position.y - msg->detections[i].bbox.size_y / 2,
        msg->detections[i].bbox.size_x,
        msg->detections[i].bbox.size_y);
    }
  } while (0);

  if (rval < 0) {
    throw std::runtime_error("IavOsdNode::drawDetectionResult() failed.");
  }
}

void IavOsdNode::detectionCallback(const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(msg_mutex_);
  detection_list_.push_back(msg);
}

void IavOsdNode::drawFaceDetectionResult(
  const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg,
  const cooper_ros_msgs::msg::LandmarkDetection2DArray::ConstSharedPtr & landmark_msg)
{
  int rval = 0;
  char text[256];
  int bbox_thickness = 4;
  int landmark_size = bbox_thickness * 2;

  do {
    EA_R_ASSERT(msg->detections.size() == landmark_msg->detections.size());

    float landmark_rect_w = landmark_size / ea_display_obj_params(display_)->dis_win_w;
    float landmark_rect_h = landmark_size / ea_display_obj_params(display_)->dis_win_h;
    landmark_rect_w = std::min(landmark_rect_w, 1.0f);
    landmark_rect_h = std::min(landmark_rect_h, 1.0f);

    ea_display_obj_params(display_)->obj_win_w = 1.0;
    ea_display_obj_params(display_)->obj_win_h = 1.0;
    ea_display_obj_params(display_)->border_thickness = bbox_thickness;
    ea_display_obj_params(display_)->font_size = 32;
    ea_display_obj_params(display_)->text_color = EA_16_COLORS_WHITE;

    for (size_t i = 0; i < msg->detections.size(); ++i) {
      snprintf(text, 256, "%.3f", msg->detections[i].results[0].hypothesis.score);

      ea_display_obj_params(display_)->box_color = EA_16_COLORS_BLUE;
      ea_display_obj_params(display_)->box_background_transparency = 0;
      ea_display_set_bbox(
        display_,
        text,
        msg->detections[i].bbox.center.position.x - msg->detections[i].bbox.size_x / 2,
        msg->detections[i].bbox.center.position.y - msg->detections[i].bbox.size_y / 2,
        msg->detections[i].bbox.size_x,
        msg->detections[i].bbox.size_y);

      for (size_t k = 0; k < landmark_msg->detections[i].points.size(); ++k) {
        ea_display_obj_params(display_)->box_color =
          (ea_16_colors_t)((EA_16_COLORS_GREEN + k) % EA_16_COLORS_MAX_NUM);
        ea_display_obj_params(display_)->box_background_transparency = 255;
        float landmark_rect_x =
          std::max((float)landmark_msg->detections[i].points[k].x - landmark_rect_w / 2.0f, 0.0f);
        float landmark_rect_y =
          std::max((float)landmark_msg->detections[i].points[k].y - landmark_rect_h / 2.0f, 0.0f);
        if (landmark_rect_x + landmark_rect_w > 1.0f || landmark_rect_y + landmark_rect_h > 1.0f) {
          continue;
        }

        ea_display_set_bbox(
          display_,
          "",
          landmark_rect_x,
          landmark_rect_y,
          landmark_rect_w,
          landmark_rect_h);
      }
    }
  } while (0);

  if (rval < 0) {
    throw std::runtime_error("IavOsdNode::drawFaceDetectionResult() failed.");
  }
}

void IavOsdNode::faceDetectionCallback(
  const vision_msgs::msg::Detection2DArray::ConstSharedPtr & msg,
  const cooper_ros_msgs::msg::LandmarkDetection2DArray::ConstSharedPtr & landmark_msg)
{
  std::lock_guard<std::mutex> lock(msg_mutex_);
  face_detection_list_.push_back(std::make_pair(msg, landmark_msg));
}

void IavOsdNode::drawClassificationResult(
  const vision_msgs::msg::Classification::ConstSharedPtr & msg)
{
  int rval = 0;
  char text[256];
  float dis_win_w = ea_display_obj_params(display_)->dis_win_w;
  float dis_win_h = ea_display_obj_params(display_)->dis_win_h;
  float x_offset = 10.0f;
  float y_offset = 10.0f;
  int font_size = 32;
  float h = font_size * 1.5f;

  do {
    EA_R_ASSERT(x_offset < dis_win_w);
    EA_R_ASSERT(y_offset < dis_win_h);

    ea_display_obj_params(display_)->obj_win_w = dis_win_w;
    ea_display_obj_params(display_)->obj_win_h = dis_win_h;
    ea_display_obj_params(display_)->font_size = font_size;
    ea_display_obj_params(display_)->text_color = EA_16_COLORS_WHITE;
    ea_display_obj_params(display_)->border_thickness = 0;

    for (size_t i = 0; i < msg->results.size(); ++i) {
      if (y_offset + h > dis_win_h) {
        break;
      }

      snprintf(text, 256, "%.3f %s", msg->results[i].score, msg->results[i].class_id.c_str());
      ea_display_set_textbox(display_, text, x_offset, y_offset, dis_win_w - x_offset, h);
      y_offset += h;
    }
  } while (0);

  if (rval < 0) {
    throw std::runtime_error("IavOsdNode::drawClassificationResult() failed.");
  }
}

void IavOsdNode::classificationCallback(
  const vision_msgs::msg::Classification::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(msg_mutex_);
  classification_list_.push_back(msg);
}

int IavOsdNode::draw256ColorsImage(
  uint8_t * buffer, int w, int h, int pitch, int rotate_type, void * arg)
{
  int rval = 0;
  ea_tensor_t * output = NULL;

  do {
    output = (ea_tensor_t *)arg;
    EA_R_ASSERT(output != NULL);
    EA_R_ASSERT(rotate_type == 0);
    cv::Mat src_img(
      ea_tensor_shape(output)[EA_H],
      ea_tensor_shape(output)[EA_W],
      CV_8UC1,
      ea_tensor_data(output),
      ea_tensor_pitch(output));
    cv::Mat dst_img(h, w, CV_8UC1, buffer, pitch);
    cv::resize(src_img, dst_img, dst_img.size(), 0.0, 0.0, cv::INTER_NEAREST);
  } while (0);

  return rval;
}

void IavOsdNode::drawSegmentationResult(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  int rval = 0;
  size_t shape[EA_DIM];

  do {
    shape[EA_N] = 1;
    shape[EA_C] = 1;
    shape[EA_H] = msg->height;
    shape[EA_W] = msg->width;
    if (segmentation_tensor_ != NULL) {
      ea_tensor_free(segmentation_tensor_);
      segmentation_tensor_ = NULL;
    }
    segmentation_tensor_ = ea_tensor_new(EA_U8, shape, 0);
    EA_R_ASSERT(segmentation_tensor_ != NULL);
    EA_R_OK(copyImageToTensor(msg, segmentation_tensor_, false));
    EA_R_OK(
      ea_display_set_256_colors_image_with_cb(display_, &draw256ColorsImage, segmentation_tensor_));
  } while (0);

  if (rval < 0) {
    if (segmentation_tensor_ != NULL) {
      ea_tensor_free(segmentation_tensor_);
      segmentation_tensor_ = NULL;
    }

    throw std::runtime_error("IavOsdNode::drawSegmentationResult() failed.");
  }
}

void IavOsdNode::drawSegmentationResultCleanup()
{
  int rval = 0;

  if (segmentation_tensor_ != NULL) {
    ea_tensor_free(segmentation_tensor_);
    segmentation_tensor_ = NULL;
  }

  do {
    EA_R_OK(ea_display_set_256_colors_image_with_cb(display_, NULL, NULL));
  } while (0);
}

void IavOsdNode::segmentationCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(msg_mutex_);
  segmentation_list_.push_back(msg);
}

void IavOsdNode::timerCallback()
{
  bool need_refresh = false;
  bool update_segmentation = false;

  std::lock_guard<std::mutex> lock(msg_mutex_);

  if (!segmentation_list_.empty()) {
    drawSegmentationResult(segmentation_list_.back());
    segmentation_list_.clear();
    need_refresh = true;
    update_segmentation = true;
  }

  if (!detection_list_.empty()) {
    drawDetectionResult(detection_list_.back());
    detection_list_.clear();
    need_refresh = true;
  }

  if (!face_detection_list_.empty()) {
    drawFaceDetectionResult(face_detection_list_.back().first, face_detection_list_.back().second);
    face_detection_list_.clear();
    need_refresh = true;
  }

  if (!classification_list_.empty()) {
    drawClassificationResult(classification_list_.back());
    classification_list_.clear();
    need_refresh = true;
  }

  if (need_refresh) {
    ea_display_refresh(display_, (void *)(unsigned long)0 /*dsp_pts*/);
  }

  if (update_segmentation) {
    drawSegmentationResultCleanup();
  }
}

}  // namespace iav_osd
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::iav_osd::IavOsdNode)
