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

#ifndef COOPER_ROS__LLAVA__LLAVA_NODE_HPP_
#define COOPER_ROS__LLAVA__LLAVA_NODE_HPP_

#include "cooper_ros_llava/llava_net.h"

#include <chrono>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

namespace cooper_ros
{
namespace llava
{

/**
 * @brief LLaVA-OneVision-7B ROS2 Node for Vision-Language Model processing
 *
 * This node handles:
 * - Image input from camera capture or file reading
 * - User text questions/prompts
 * - VLM processing using LLaVA-OneVision-7B model
 * - Response generation and performance monitoring
 */
class LlavaNode : public rclcpp::Node
{
public:
  /**
   * @brief Constructor
   * @param options Node options
   */
  explicit LlavaNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

  /**
   * @brief Destructor
   */
  ~LlavaNode();

private:
  /**
   * @brief Initialize node parameters
   */
  void initParameters();

  /**
   * @brief Initialize ROS interfaces (subscribers, publishers)
   */
  void initRosInterfaces();

  /**
   * @brief Initialize LLaVA model using LLaVANet class
   */
  void initLlavaModel();

  /**
   * @brief Image callback function
   * @param msg Image message
   */
  void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg);

  /**
   * @brief User content callback function
   * @param msg String message containing user question
   */
  void userContentCallback(const std_msgs::msg::String::ConstSharedPtr & msg);

  /**
   * @brief Process VLM request with user question
   * @param user_question User's question string
   */
  void processVlmRequest(const std::string & user_question);

  /**
   * @brief Process image for LLaVA processing
   */
  void processImageForLlava();

  /**
   * @brief Static callback function for LLaVA stream output
   * @param token Token string from LLaVA
   */
  static void streamCallback(const char * token);

  /**
   * @brief Set the question for LLaVA processing
   * @param question Question string
   */
  void setQuestion(const std::string & question);

  /**
   * @brief Set the image for LLaVA processing
   * @param image Image
   */
  void setImage(const cv::Mat & image);

  /**
   * @brief Set the stream callback for LLaVA processing
   * @param callback Stream callback function
   */
  void setStreamCallback(void (*callback)(const char * token));

  // ROS2 Subscribers
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr user_content_subscription_;

  // ROS2 Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr response_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr performance_publisher_;

  // Configuration parameters
  bool enable_performance_monitor_;
  int max_response_length_;
  double temperature_;

  // LLaVA model interface (now using function calls)
  std::unique_ptr<llava_params_t> params_;
  llava_ctx_t * llava_ctx_;

  // String storage for CLI parameters (to avoid strdup/free)
  std::string base_path_str_;
  std::string vit_path_str_;
  std::string vit_video_path_str_;
  std::string vit_single_path_str_;
  std::string default_prompt_str_;

  // Model and processing state
  bool model_loaded_;
  bool image_processed_;

  // Current image data
  cv::Mat current_image_;
  rclcpp::Time last_image_timestamp_;
};

}  // namespace llava
}  // namespace cooper_ros

#endif  // COOPER_ROS_LLAVA__LLAVA_NODE_HPP_
