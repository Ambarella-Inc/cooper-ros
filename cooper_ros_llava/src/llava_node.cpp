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

#include "cooper_ros_llava/llava_node.hpp"
#include "cooper_ros_llava/llava_net.h"

#include <sensor_msgs/image_encodings.hpp>

#ifdef USE_CV_BRIDGE_H
#include <cv_bridge/cv_bridge.h>
#else
#include <cv_bridge/cv_bridge.hpp>
#endif

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <sstream>

#define PUB_TOPIC_NAME_RESPONSE     "llava/response"
#define PUB_TOPIC_NAME_PERFORMANCE  "llava/performance"
#define SUB_TOPIC_NAME_IMAGE        "image"
#define SUB_TOPIC_NAME_USER_CONTENT "user_content"

#define PARAM_NAME_LLM_MODE                   "llm_mode"
#define PARAM_NAME_BASE_PATH                  "base_path"
#define PARAM_NAME_VIT_PATH                   "vit_path"
#define PARAM_NAME_VIT_VIDEO_PATH             "vit_video_path"
#define PARAM_NAME_VIT_SINGLE_PATH            "vit_single_path"
#define PARAM_NAME_DEVICE                     "device"
#define PARAM_NAME_ENABLE_PERFORMANCE_MONITOR "enable_performance_monitor"
#define PARAM_NAME_MAX_RESPONSE_LENGTH        "max_response_length"
#define PARAM_NAME_TEMPERATURE                "temperature"
#define PARAM_NAME_DEFAULT_PROMPT             "default_prompt"
#define PARAM_NAME_MAX_IMAGE_FRAMES           "max_image_frames"
#define PARAM_NAME_MAX_VIDEO_FRAMES           "max_video_frames"
#define PARAM_NAME_MAX_RECORD_FRAMES          "max_record_frames"
#define PARAM_NAME_DISABLE_DSP                "disable_dsp"
#define PARAM_NAME_BATCH_SIZE                 "batch_size"

namespace cooper_ros
{
namespace llava
{

// Global variable to store current node instance for callback
static LlavaNode * g_current_node = nullptr;

LlavaNode::LlavaNode(const rclcpp::NodeOptions & options)
: Node("llava", options),
  model_loaded_(false),
  image_processed_(false)
{
  RCLCPP_INFO(this->get_logger(), "Initializing LLaVA-OneVision-7B Node...");

  g_current_node = this;  // Set global node instance for callback

  initParameters();       // Initialize parameters

  initRosInterfaces();    // Initialize ROS interfaces

  initLlavaModel();       // Initialize LLaVA model

  RCLCPP_INFO(this->get_logger(), "LLaVA-OneVision-7B Node initialized successfully");
}

LlavaNode::~LlavaNode()
{
  RCLCPP_INFO(this->get_logger(), "Shutting down LLaVA-OneVision-7B Node");

  if (llava_ctx_) {
    llavaNetFree(llava_ctx_);
  }

  if (g_current_node == this) {
    g_current_node = nullptr;  // Clear global node instance
  }
}

void LlavaNode::initParameters()
{
  // Model parameters
  this->declare_parameter(PARAM_NAME_LLM_MODE, 1);  // Default to LLAVA_OV
  this->declare_parameter(PARAM_NAME_BASE_PATH, "./");
  this->declare_parameter(PARAM_NAME_VIT_PATH, "");
  this->declare_parameter(PARAM_NAME_VIT_VIDEO_PATH, "");
  this->declare_parameter(PARAM_NAME_VIT_SINGLE_PATH, "");
  this->declare_parameter(PARAM_NAME_DEVICE, 2);  // Auto device
  this->declare_parameter(PARAM_NAME_ENABLE_PERFORMANCE_MONITOR, true);
  this->declare_parameter(PARAM_NAME_MAX_RESPONSE_LENGTH, 512);
  this->declare_parameter(PARAM_NAME_TEMPERATURE, 0.7);
  this->declare_parameter(PARAM_NAME_DEFAULT_PROMPT, "");
  this->declare_parameter(PARAM_NAME_MAX_IMAGE_FRAMES, 8);
  this->declare_parameter(PARAM_NAME_MAX_VIDEO_FRAMES, 32);
  this->declare_parameter(PARAM_NAME_MAX_RECORD_FRAMES, 32 * 30);
  this->declare_parameter(PARAM_NAME_DISABLE_DSP, true);  // Disable DSP for ROS node
  this->declare_parameter(PARAM_NAME_BATCH_SIZE, 64);

  // Initialize parameters structure
  params_ = std::make_unique<llava_params_t>();
  memset(params_.get(), 0, sizeof(llava_params_t));

  // Set parameters from ROS parameters
  params_->llm_mode = this->get_parameter(PARAM_NAME_LLM_MODE).as_int();

  // Store strings in std::string members, then point to their c_str()
  base_path_str_ = this->get_parameter(PARAM_NAME_BASE_PATH).as_string();
  params_->base_path = base_path_str_.c_str();

  vit_path_str_ = this->get_parameter(PARAM_NAME_VIT_PATH).as_string();
  if (!vit_path_str_.empty()) {
    params_->vit_path = vit_path_str_.c_str();
    params_->vit_num++;
  } else {
    params_->vit_path = nullptr;
  }

  vit_video_path_str_ = this->get_parameter(PARAM_NAME_VIT_VIDEO_PATH).as_string();
  if (!vit_video_path_str_.empty()) {
    params_->vit_video_path = vit_video_path_str_.c_str();
    params_->vit_num++;
  } else {
    params_->vit_video_path = nullptr;
  }

  vit_single_path_str_ = this->get_parameter(PARAM_NAME_VIT_SINGLE_PATH).as_string();
  if (!vit_single_path_str_.empty()) {
    params_->vit_single_path = vit_single_path_str_.c_str();
    params_->vit_num++;
  } else {
    params_->vit_single_path = nullptr;
  }

  params_->device = this->get_parameter(PARAM_NAME_DEVICE).as_int();

  default_prompt_str_ = this->get_parameter(PARAM_NAME_DEFAULT_PROMPT).as_string();
  params_->default_prompt = default_prompt_str_.c_str();
  params_->max_image_frames = this->get_parameter(PARAM_NAME_MAX_IMAGE_FRAMES).as_int();
  params_->max_video_frames = this->get_parameter(PARAM_NAME_MAX_VIDEO_FRAMES).as_int();
  params_->max_record_frames = this->get_parameter(PARAM_NAME_MAX_RECORD_FRAMES).as_int();
  params_->disable_dsp = this->get_parameter(PARAM_NAME_DISABLE_DSP).as_bool();
  params_->batch_size = this->get_parameter(PARAM_NAME_BATCH_SIZE).as_int();

  enable_performance_monitor_ =
    this->get_parameter(PARAM_NAME_ENABLE_PERFORMANCE_MONITOR).as_bool();
  max_response_length_ = this->get_parameter(PARAM_NAME_MAX_RESPONSE_LENGTH).as_int();
  temperature_ = this->get_parameter(PARAM_NAME_TEMPERATURE).as_double();

  RCLCPP_INFO(this->get_logger(), "LLM Mode: %d", params_->llm_mode);
  RCLCPP_INFO(this->get_logger(), "Base Path: %s", params_->base_path);
  RCLCPP_INFO(this->get_logger(), "VIT Path: %s", params_->vit_path ? params_->vit_path : "NULL");
  RCLCPP_INFO(
    this->get_logger(),
    "VIT Video Path: %s",
    params_->vit_video_path ? params_->vit_video_path : "NULL");
  RCLCPP_INFO(
    this->get_logger(),
    "VIT Single Path: %s",
    params_->vit_single_path ? params_->vit_single_path : "NULL");
  RCLCPP_INFO(this->get_logger(), "VIT Number: %d", params_->vit_num);
  RCLCPP_INFO(this->get_logger(), "Device ID: %d", params_->device);
}

void LlavaNode::initLlavaModel()
{
  if (params_->vit_num == 0) {
    RCLCPP_ERROR(this->get_logger(), "No VIT model paths specified!");
    return;
  }

  try {
    RCLCPP_INFO(this->get_logger(), "Initializing LLaVA model...");

    // Initialize with parameters using function call
    llava_ctx_ = llavaNetNew(params_.get());
    if (!llava_ctx_) {
      RCLCPP_ERROR(this->get_logger(), "Failed to initialize LLaVA model");
      model_loaded_ = false;
      return;
    }

    setStreamCallback(streamCallback);

    model_loaded_ = true;
    RCLCPP_INFO(this->get_logger(), "LLaVA-OneVision-7B model loaded successfully");

  } catch (const std::exception & e) {
    RCLCPP_ERROR(this->get_logger(), "Failed to load LLaVA model: %s", e.what());
    model_loaded_ = false;
  }
}

void LlavaNode::initRosInterfaces()
{
  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  // Subscribers
  image_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
    SUB_TOPIC_NAME_IMAGE,
    reliable_qos,
    std::bind(&LlavaNode::imageCallback, this, std::placeholders::_1));

  user_content_subscription_ = this->create_subscription<std_msgs::msg::String>(
    SUB_TOPIC_NAME_USER_CONTENT,
    reliable_qos,
    std::bind(&LlavaNode::userContentCallback, this, std::placeholders::_1));

  // Publishers
  response_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_RESPONSE, reliable_qos);
  performance_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_PERFORMANCE, reliable_qos);

  RCLCPP_INFO(this->get_logger(), "Subscribed to: image, user_content");
  RCLCPP_INFO(this->get_logger(), "Publishing to: llava/response, llava/performance");
}

void LlavaNode::imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  RCLCPP_INFO(
    this->get_logger(),
    "DEBUG: Image size: %dx%d, encoding: %s",
    msg->width,
    msg->height,
    msg->encoding.c_str());

  if (!model_loaded_) {
    RCLCPP_WARN(this->get_logger(), "Model not loaded, ignoring image");
    return;
  }

  try {
    // Convert ROS image to OpenCV format
    cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
    current_image_ = cv_ptr->image.clone();

    RCLCPP_INFO(
      this->get_logger(),
      "Received image: %dx%d",
      current_image_.cols,
      current_image_.rows);

    // Store timestamp for performance monitoring
    last_image_timestamp_ = this->now();

    // Process the image through LLaVA preprocessing
    processImageForLlava();

  } catch (cv_bridge::Exception & e) {
    RCLCPP_ERROR(this->get_logger(), "CV bridge exception: %s", e.what());
  }
}

void LlavaNode::userContentCallback(const std_msgs::msg::String::ConstSharedPtr & msg)
{
  RCLCPP_INFO(this->get_logger(), "DEBUG: User content: '%s'", msg->data.c_str());

  if (!model_loaded_) {
    RCLCPP_WARN(this->get_logger(), "Model not loaded, cannot process user content");
    auto response_msg = std_msgs::msg::String();
    response_msg.data = "Error: Model failed to load. Please check the configuration.";
    response_publisher_->publish(response_msg);
    response_msg.data = "<EOS>";
    response_publisher_->publish(response_msg);
    return;
  }

  if (current_image_.empty()) {
    RCLCPP_WARN(this->get_logger(), "No image available for VLM processing");

    // Send error response
    auto response_msg = std_msgs::msg::String();
    response_msg.data =
      "Error: No image available. Please capture camera or read image file "
      "first.";
    response_publisher_->publish(response_msg);
    response_msg.data = "<EOS>";
    response_publisher_->publish(response_msg);
    return;
  }

  std::string user_question = msg->data;
  RCLCPP_INFO(this->get_logger(), "Processing user question: %s", user_question.c_str());

  // Process with LLaVA model
  processVlmRequest(user_question);
}

void LlavaNode::processVlmRequest(const std::string & user_question)
{
  try {
    if (!image_processed_) {
      RCLCPP_ERROR(this->get_logger(), "No image processed, cannot process user question");
      return;
    }

    setQuestion(user_question);

    // Run LLaVA inference
    int ret = runLlavaInference(llava_ctx_);

    if (ret == 0) {
      // Publish EOS marker to indicate end of streaming
      auto eof_msg = std_msgs::msg::String();
      eof_msg.data = "<EOS>";
      response_publisher_->publish(eof_msg);

      // RCLCPP_INFO(this->get_logger(), "Generated response successfully");

      // Get performance metrics from LLaVA
      llava_performance_t * perf = getPerformance(llava_ctx_);
      if (enable_performance_monitor_ && perf) {
        auto fmt_func = [](double v) {
          std::ostringstream oss;
          oss << std::fixed << std::setprecision(2) << v;
          return oss.str();
        };

        nlohmann::json performanceJson;
        performanceJson["image"] = fmt_func(perf->vit_perf) + " s";
        performanceJson["input"] = fmt_func(perf->input_perf) + " s";
        performanceJson["output"] = fmt_func(perf->output_perf) + " s";
        performanceJson["prefill"] = fmt_func(perf->prefill_tokens) + " token/s";
        performanceJson["generate"] = fmt_func(perf->generate_tokens) + " token/s";

        auto message = std_msgs::msg::String();
        message.data = performanceJson.dump();
        performance_publisher_->publish(message);
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "LLaVA inference failed");

      auto error_msg = std_msgs::msg::String();
      error_msg.data = "Error: Failed to process the question. Please try again.";
      response_publisher_->publish(error_msg);
      error_msg.data = "<EOS>";
      response_publisher_->publish(error_msg);
    }

  } catch (const std::exception & e) {
    RCLCPP_ERROR(this->get_logger(), "Error processing VLM request: %s", e.what());

    auto error_msg = std_msgs::msg::String();
    error_msg.data = "Error: Failed to process the question. Please try again.";
    response_publisher_->publish(error_msg);
    error_msg.data = "<EOS>";
    response_publisher_->publish(error_msg);
  }
}

void LlavaNode::processImageForLlava()
{
  if (current_image_.empty() || !model_loaded_) {
    return;
  }

  try {
    setImage(current_image_);
    // Process the image
    int ret = processImage(llava_ctx_);

    if (ret == 0) {
      image_processed_ = true;
      RCLCPP_INFO(this->get_logger(), "Image processed successfully");
    } else {
      RCLCPP_ERROR(this->get_logger(), "Image processing failed");
    }

  } catch (const std::exception & e) {
    RCLCPP_ERROR(this->get_logger(), "Error processing image for LLaVA: %s", e.what());
  }
}

void LlavaNode::streamCallback(const char * token)
{
  if (g_current_node) {
    auto stream_msg = std_msgs::msg::String();
    stream_msg.data = token;
    g_current_node->response_publisher_->publish(stream_msg);
  }
}

void LlavaNode::setQuestion(const std::string & question)
{
  void * question_ptr = getCurrentQuestionPtr(llava_ctx_);
  if (question_ptr) {
    *(std::string *)question_ptr = question;
  }
}

void LlavaNode::setImage(const cv::Mat & image)
{
  void * image_ptr = getCurrentImagePtr(llava_ctx_);
  if (image_ptr) {
    *(cv::Mat *)image_ptr = image;
  }
}

void LlavaNode::setStreamCallback(llava_stream_callback_t callback)
{
  llava_stream_callback_t * callback_ptr = getStreamCallbackPtr(llava_ctx_);
  if (callback_ptr) {
    *callback_ptr = callback;
  }
}

}  // namespace llava
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::llava::LlavaNode)
