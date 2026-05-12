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

#ifndef COOPER_ROS__CLIP__CLIP_NODE_HPP_
#define COOPER_ROS__CLIP__CLIP_NODE_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <nlohmann/json.hpp>

#include <eazyai.h>

#include "cooper_ros_clip/clip_net.h"

namespace cooper_ros
{

namespace clip
{

/**
 * @brief Long-CLIP ROS2 Node for File mode image retrieval and classification
 *
 * This node handles three search modes:
 * - Text-to-Image: Find images based on text descriptions
 * - Image-to-Text: Find text descriptions that match an image
 * - Image-to-Image: Find similar images using visual similarity
 */
class ClipNode : public rclcpp::Node
{
public:
  /**
   * @brief Constructor
   * @param options Node options
   */
  explicit ClipNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

  /**
   * @brief Destructor
   */
  ~ClipNode();

private:
  /**
   * @brief Initialize node parameters
   */
  void initParameters();

  /**
   * @brief Initialize network
   */
  void initNetwork();

  /**
   * @brief Initialize ROS2 interfaces (publishers/subscribers)
   */
  void initRosInterfaces();

  /**
   * @brief Callback for JSON request processing
   * @param msg JSON request message
   */
  void requestCallback(const std_msgs::msg::String::ConstSharedPtr & msg);

  /**
   * @brief Process text-to-image search request
   * @param request JSON request containing image_dir and text
   */
  void processTextToImageRequest(const nlohmann::json & request);

  /**
   * @brief Process image-to-text matching request
   * @param request JSON request containing text_file and image_file
   */
  void processImageToTextRequest(const nlohmann::json & request);

  /**
   * @brief Process image-to-image search request
   * @param request JSON request containing image_dir and image_file
   */
  void processImageToImageRequest(const nlohmann::json & request);

  /**
   * @brief Run network inference and format results
   * @param mode CLIP mode to determine inference type and output formatting
   * @param results Reference to store the formatted results string
   * @return true on success, false on failure
   */
  bool runNetwork(clip_mode_t mode, std::string & results);

  /**
   * @brief Process CLIP inference based on mode
   * @param mode CLIP mode to execute (RETRIEVAL, CLASSIFICATION, or SEARCH)
   * @return 0 on success, negative value on failure
   */
  int processClipInference(clip_mode_t mode);

  /**
   * @brief Convert confidence levels to formatted string
   * @param mode CLIP mode for result formatting (affects output format)
   * @return Formatted results string with confidence scores and metadata
   */
  std::string convertConfidenceLevelsToString(clip_mode_t mode);

  /**
   * @brief Validate file existence and type
   * @param file_path Path to the file to validate
   * @param results Reference to store error message if validation fails
   * @return true if file is valid, false otherwise
   */
  bool validateFile(const std::string & file_path, std::string & results);

  /**
   * @brief Validate directory existence and type
   * @param dir_path Path to the directory to validate
   * @param results Reference to store error message if validation fails
   * @return true if directory is valid, false otherwise
   */
  bool validateDirectory(const std::string & dir_path, std::string & results);

  /**
   * @brief Perform text-to-image search using Long-CLIP
   * @param query_text Query text description to search for
   * @param target_image_dir Directory containing target images to search
   * @param results Reference to store the search results string
   * @return true on success, false on failure (invalid inputs or inference errors)
   */
  bool searchTextToImage(
    const std::string & query_text, const std::string & target_image_dir, std::string & results);

  /**
   * @brief Perform image-to-text matching using Long-CLIP
   * @param query_image_path Path to the query image file
   * @param target_text_file Path to the file containing target text descriptions
   * @param results Reference to store the matching results string
   * @return true on success, false on failure (invalid inputs or inference errors)
   */
  bool searchImageToText(
    const std::string & query_image_path,
    const std::string & target_text_file,
    std::string & results);

  /**
   * @brief Perform image-to-image search using Long-CLIP
   * @param query_image_path Path to the query image file
   * @param target_image_dir Directory containing target images to search
   * @param results Reference to store the similarity search results string
   * @return true on success, false on failure (invalid inputs or inference errors)
   */
  bool searchImageToImage(
    const std::string & query_image_path,
    const std::string & target_image_dir,
    std::string & results);

  /**
   * @brief Publish performance metrics
   * @param operation Operation name
   */
  void publishPerformance(const std::string & operation);

  // ROS2 Subscribers
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr request_subscription_;

  // ROS2 Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr image_path_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr text_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr performance_publisher_;

  // Model parameters
  std::string image_encoder_path_;
  std::string text_encoder_path_;
  std::string text_embedding_path_;
  std::string vocab_path_;
  bool enable_performance_monitor_;
  int max_results_;
  int top_k_;
  uint32_t batch_num_;
  // Model state
  clip_context_t * clip_ctx_;  // CLIP context for inference
  clip_params_t clip_params_;  // CLIP parameters
  bool model_loaded_;          // Flag indicating if models are loaded successfully

  // Timing metrics for performance monitoring
  uint64_t image_receive_time_us_;  // Timestamp when image request was received
  uint64_t image_delay_time_us_;    // Delay time from request to processing
};

}  // namespace clip
}  // namespace cooper_ros

#endif  // COOPER_ROS__CLIP__CLIP_NODE_HPP_
