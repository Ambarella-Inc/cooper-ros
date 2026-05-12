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

#include "cooper_ros_clip/clip_node.hpp"

#include <filesystem>

#define PUB_TOPIC_NAME_IMAGE_PATH  "clip/image_path"
#define PUB_TOPIC_NAME_TEXT        "clip/text"
#define PUB_TOPIC_NAME_PERFORMANCE "clip/performance"
#define SUB_TOPIC_NAME_REQUEST     "request"

#define PARAM_NAME_ENABLE_PERFORMANCE_MONITOR "enable_performance_monitor"
#define PARAM_NAME_MAX_RESULTS                "max_results"
#define PARAM_NAME_TOP_K                      "top_k"
#define PARAM_NAME_BATCH_NUM                  "batch_num"
#define PARAM_NAME_IMAGE_ENCODER_PATH         "image_encoder_path"
#define PARAM_NAME_TEXT_ENCODER_PATH          "text_encoder_path"
#define PARAM_NAME_TEXT_EMBEDDING_PATH        "text_embedding_path"
#define PARAM_NAME_VOCAB_PATH                 "vocab_path"

namespace cooper_ros
{
namespace clip
{

ClipNode::ClipNode(const rclcpp::NodeOptions & options)
: Node("clip", options),
  model_loaded_(false)
{
  RCLCPP_INFO(this->get_logger(), "Initializing Long-CLIP Node...");

  // Initialize parameters
  initParameters();

  // Initialize EazyAI
  initNetwork();

  // Initialize ROS interfaces
  initRosInterfaces();

  RCLCPP_INFO(this->get_logger(), "Long-CLIP Node initialized successfully");
}

ClipNode::~ClipNode()
{
  RCLCPP_INFO(this->get_logger(), "Shutting down Long-CLIP Node");
}

void ClipNode::initParameters()
{
  // Model parameters
  this->declare_parameter(PARAM_NAME_ENABLE_PERFORMANCE_MONITOR, true);
  this->declare_parameter(PARAM_NAME_MAX_RESULTS, 10);
  this->declare_parameter(PARAM_NAME_TOP_K, 5);
  this->declare_parameter(PARAM_NAME_BATCH_NUM, 1);

  this->declare_parameter(PARAM_NAME_IMAGE_ENCODER_PATH, "");
  this->declare_parameter(PARAM_NAME_TEXT_ENCODER_PATH, "");
  this->declare_parameter(PARAM_NAME_TEXT_EMBEDDING_PATH, "");
  this->declare_parameter(PARAM_NAME_VOCAB_PATH, "");

  enable_performance_monitor_ =
    this->get_parameter(PARAM_NAME_ENABLE_PERFORMANCE_MONITOR).as_bool();
  max_results_ = this->get_parameter(PARAM_NAME_MAX_RESULTS).as_int();
  top_k_ = this->get_parameter(PARAM_NAME_TOP_K).as_int();
  batch_num_ = this->get_parameter(PARAM_NAME_BATCH_NUM).as_int();

  image_encoder_path_ = this->get_parameter(PARAM_NAME_IMAGE_ENCODER_PATH).as_string();
  text_encoder_path_ = this->get_parameter(PARAM_NAME_TEXT_ENCODER_PATH).as_string();
  text_embedding_path_ = this->get_parameter(PARAM_NAME_TEXT_EMBEDDING_PATH).as_string();
  vocab_path_ = this->get_parameter(PARAM_NAME_VOCAB_PATH).as_string();

  RCLCPP_INFO(this->get_logger(), "Model Configuration:");
  RCLCPP_INFO(this->get_logger(), "  Image Encoder: %s", image_encoder_path_.c_str());
  RCLCPP_INFO(this->get_logger(), "  Text Encoder: %s", text_encoder_path_.c_str());
  RCLCPP_INFO(this->get_logger(), "  Text Embedding: %s", text_embedding_path_.c_str());
  RCLCPP_INFO(this->get_logger(), "  Vocab: %s", vocab_path_.c_str());
  RCLCPP_INFO(this->get_logger(), "  Top K: %d, Batch Num: %d", top_k_, batch_num_);
}

void ClipNode::initNetwork()
{
  if (image_encoder_path_.empty() || text_encoder_path_.empty() || text_embedding_path_.empty()) {
    RCLCPP_ERROR(this->get_logger(), "Model paths not specified!");
    return;
  }

  try {
    memset(&clip_params_, 0, sizeof(clip_params_t));
    clip_params_.log_level = EA_LOG_LEVEL_NOTICE;
    clip_params_.mode = CLIP_MODE_UNKNOWN;
    clip_params_.top_k = top_k_;
    clip_params_.batch_num = batch_num_;
    clip_params_.image_model_path = image_encoder_path_.c_str();
    clip_params_.text_model_path = text_encoder_path_.c_str();
    clip_params_.text_embedded_weight_path = text_embedding_path_.c_str();
    clip_params_.vocab_path = vocab_path_.c_str();

    model_loaded_ = true;
    RCLCPP_INFO(this->get_logger(), "Long-CLIP models loaded successfully");

  } catch (const std::exception & e) {
    RCLCPP_ERROR(this->get_logger(), "Failed to load Long-CLIP models: %s", e.what());
    model_loaded_ = false;
  }
}

void ClipNode::initRosInterfaces()
{
  auto reliable_qos = rclcpp::QoS(
    rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default),
    rmw_qos_profile_default);

  request_subscription_ = this->create_subscription<std_msgs::msg::String>(
    SUB_TOPIC_NAME_REQUEST,
    reliable_qos,
    std::bind(&ClipNode::requestCallback, this, std::placeholders::_1));

  image_path_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_IMAGE_PATH, reliable_qos);
  text_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_TEXT, reliable_qos);
  performance_publisher_ =
    this->create_publisher<std_msgs::msg::String>(PUB_TOPIC_NAME_PERFORMANCE, reliable_qos);

  RCLCPP_INFO(this->get_logger(), "ROS interfaces initialized");
  RCLCPP_INFO(this->get_logger(), "Subscribed to: %s", SUB_TOPIC_NAME_REQUEST);
  RCLCPP_INFO(
    this->get_logger(),
    "Publishing to: %s, %s, %s",
    PUB_TOPIC_NAME_IMAGE_PATH,
    PUB_TOPIC_NAME_TEXT,
    PUB_TOPIC_NAME_PERFORMANCE);
}

void ClipNode::requestCallback(const std_msgs::msg::String::ConstSharedPtr & msg)
{
  image_receive_time_us_ = this->now().nanoseconds() / 1000;

  if (!model_loaded_) {
    RCLCPP_WARN(this->get_logger(), "Models not loaded, ignoring request");
    return;
  }

  try {
    nlohmann::json request;

    try {
      request = nlohmann::json::parse(msg->data);
    } catch (const nlohmann::json::parse_error & e) {
      RCLCPP_ERROR(this->get_logger(), "Failed to parse JSON request");
      return;
    }

    image_delay_time_us_ = 0;
    if (request.contains("timestamp_us")) {
      uint64_t request_timestamp_us = request["timestamp_us"].get<uint64_t>();
      uint64_t current_time_us = image_receive_time_us_;
      image_delay_time_us_ = current_time_us - request_timestamp_us;
    } else {
      RCLCPP_WARN(this->get_logger(), "No timestamp_us found in request");
    }

    std::string search_type = request["search_type"].get<std::string>();
    RCLCPP_INFO(this->get_logger(), "Processing request: %s", search_type.c_str());

    if (search_type == "text_to_image") {
      processTextToImageRequest(request);
    } else if (search_type == "image_to_text") {
      processImageToTextRequest(request);
    } else if (search_type == "image_to_image") {
      processImageToImageRequest(request);
    } else {
      RCLCPP_ERROR(this->get_logger(), "Unknown search type: %s", search_type.c_str());
    }

  } catch (const std::exception & e) {
    RCLCPP_ERROR(this->get_logger(), "Error processing request: %s", e.what());
  }
}

void ClipNode::processTextToImageRequest(const nlohmann::json & request)
{
  std::string target_image_dir = request["image_dir"].get<std::string>();
  std::string query_text = request["text"].get<std::string>();

  RCLCPP_INFO(
    this->get_logger(),
    "Text-to-Image: '%s' in '%s'",
    query_text.c_str(),
    target_image_dir.c_str());

  std::string search_results;
  bool success = searchTextToImage(query_text, target_image_dir, search_results);

  auto result_msg = std_msgs::msg::String();
  if (success) {
    result_msg.data = search_results;
    RCLCPP_INFO(this->get_logger(), "Text-to-Image result: %s", search_results.c_str());
    publishPerformance("text_to_image");
  } else {
    result_msg.data = search_results;
    RCLCPP_ERROR(this->get_logger(), "%s", search_results.c_str());
  }
  image_path_publisher_->publish(result_msg);
}

void ClipNode::processImageToTextRequest(const nlohmann::json & request)
{
  std::string target_text_file = request["text_file"].get<std::string>();
  std::string query_image_path = request["image_file"].get<std::string>();

  RCLCPP_INFO(
    this->get_logger(),
    "Image-to-Text: '%s' with '%s'",
    query_image_path.c_str(),
    target_text_file.c_str());

  std::string search_results;
  bool success = searchImageToText(query_image_path, target_text_file, search_results);

  auto result_msg = std_msgs::msg::String();
  if (success) {
    result_msg.data = search_results;
    RCLCPP_INFO(this->get_logger(), "Image-to-Text result: %s", search_results.c_str());
    publishPerformance("image_to_text");
  } else {
    result_msg.data = search_results;
    RCLCPP_ERROR(this->get_logger(), "%s", search_results.c_str());
  }
  text_publisher_->publish(result_msg);
}

void ClipNode::processImageToImageRequest(const nlohmann::json & request)
{
  std::string target_image_dir = request["image_dir"].get<std::string>();
  std::string query_image_path = request["image_file"].get<std::string>();

  RCLCPP_INFO(
    this->get_logger(),
    "Image-to-Image: '%s' in '%s'",
    query_image_path.c_str(),
    target_image_dir.c_str());

  std::string search_results;
  bool success = searchImageToImage(query_image_path, target_image_dir, search_results);

  auto result_msg = std_msgs::msg::String();
  if (success) {
    result_msg.data = search_results;
    RCLCPP_INFO(this->get_logger(), "Image-to-Image result: %s", search_results.c_str());
    publishPerformance("image_to_image");
  } else {
    result_msg.data = search_results;
    RCLCPP_ERROR(this->get_logger(), "%s", search_results.c_str());
  }
  image_path_publisher_->publish(result_msg);
}

bool ClipNode::runNetwork(clip_mode_t mode, std::string & results)
{
  clip_ctx_ = clip_context_init(&clip_params_);
  if (clip_ctx_ == nullptr) {
    results = "ERROR: Failed to initialize CLIP context - check model paths and configuration";
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  int rval = processClipInference(mode);
  if (rval < 0) {
    clip_context_deinit(clip_ctx_);
    results = "ERROR: CLIP inference failed with return value: " + std::to_string(rval);
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  results = convertConfidenceLevelsToString(mode);
  clip_context_deinit(clip_ctx_);
  return true;
}

int ClipNode::processClipInference(clip_mode_t mode)
{
  int rval = -1;
  switch (mode) {
    case CLIP_MODE_RETRIEVAL:
      rval = clip_run_text_to_image_retrieval(clip_ctx_);
      break;
    case CLIP_MODE_CLASSIFICATION:
      rval = clip_run_image_classification(clip_ctx_);
      break;
    case CLIP_MODE_SEARCH:
      rval = clip_run_image_similarity_search(clip_ctx_);
      break;
    default:
      RCLCPP_ERROR(this->get_logger(), "Unknown CLIP mode: %d", (int)mode);
      break;
  }

  if (rval < 0) {
    RCLCPP_ERROR(this->get_logger(), "Failed to run CLIP inference");
  }
  return rval;
}

std::string ClipNode::convertConfidenceLevelsToString(clip_mode_t mode)
{
  std::string results = "";
  const confidence_levels_t * confidence_levels = clip_get_confidence_levels(clip_ctx_);
  uint32_t count = clip_get_confidence_levels_count(clip_ctx_);

  if (confidence_levels == nullptr || count == 0) {
    results =
      "ERROR: CLIP inference completed but no results were generated - check input data and model "
      "configuration";
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return results;
  }

  for (uint32_t i = 0; i < count; i++) {
    const auto & level = confidence_levels[i];
    if (mode == CLIP_MODE_SEARCH) {
      results += "[" + std::string(level.image_info) + ":  " +
                 std::to_string(static_cast<int>(level.sorce * 100) / 100.0) + "]\n";
    } else {
      const char * info = (mode == CLIP_MODE_CLASSIFICATION) ? level.prompt : level.image_info;
      results += "[<" + std::string(info) +
                 ">: (score: " + std::to_string(static_cast<int>(level.sorce * 100) / 100.0) +
                 "), (logits: " + std::to_string(static_cast<int>(level.logits * 10000) / 100.0) +
                 "%)]\n";
    }
  }

  return results;
}

bool ClipNode::validateFile(const std::string & file_path, std::string & results)
{
  if (file_path.empty()) {
    results = "ERROR: File path is empty";
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  if (!std::filesystem::exists(file_path)) {
    results = "ERROR: File does not exist: " + file_path;
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  if (!std::filesystem::is_regular_file(file_path)) {
    results = "ERROR: Path is not a file: " + file_path;
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  return true;
}

bool ClipNode::validateDirectory(const std::string & dir_path, std::string & results)
{
  if (dir_path.empty()) {
    results = "ERROR: Directory path is empty";
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  if (!std::filesystem::exists(dir_path)) {
    results = "ERROR: Directory does not exist: " + dir_path;
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  if (!std::filesystem::is_directory(dir_path)) {
    results = "ERROR: Path is not a directory: " + dir_path;
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  return true;
}

bool ClipNode::searchTextToImage(
  const std::string & query_text, const std::string & target_image_dir, std::string & results)
{
  if (query_text.empty()) {
    results = "ERROR: Query text is empty";
    RCLCPP_ERROR(this->get_logger(), "%s", results.c_str());
    return false;
  }

  if (!validateDirectory(target_image_dir, results)) {
    return false;
  }

  clip_params_.mode = CLIP_MODE_RETRIEVAL;
  clip_params_.images_dir = target_image_dir.c_str();
  clip_params_.text = query_text.c_str();
  return runNetwork(clip_params_.mode, results);
}

bool ClipNode::searchImageToText(
  const std::string & query_image_path, const std::string & target_text_file, std::string & results)
{
  if (!validateFile(query_image_path, results)) {
    return false;
  }

  if (!validateFile(target_text_file, results)) {
    return false;
  }

  clip_params_.mode = CLIP_MODE_CLASSIFICATION;
  clip_params_.image_path = query_image_path.c_str();
  clip_params_.prompts_path = target_text_file.c_str();
  return runNetwork(clip_params_.mode, results);
}

bool ClipNode::searchImageToImage(
  const std::string & query_image_path, const std::string & target_image_dir, std::string & results)
{
  if (!validateFile(query_image_path, results)) {
    return false;
  }

  if (!validateDirectory(target_image_dir, results)) {
    return false;
  }

  clip_params_.mode = CLIP_MODE_SEARCH;
  clip_params_.images_dir = target_image_dir.c_str();
  clip_params_.query_image_path = query_image_path.c_str();
  return runNetwork(clip_params_.mode, results);
}

void ClipNode::publishPerformance(const std::string & operation)
{
  if (!enable_performance_monitor_) {
    return;
  }

  auto publish_time_us = this->now().nanoseconds() / 1000;

  const clip_performance_t * performance = clip_get_performance(clip_ctx_);
  auto image_delay_us = image_delay_time_us_;
  auto preprocess_us = performance->inference_start_time_us - image_receive_time_us_;
  auto inference_us = performance->inference_us;
  auto post_process_us = publish_time_us - performance->inference_end_time_us;
  auto cvflow_us = performance->cvflow_us;

  nlohmann::json perf_data;
  perf_data["operation"] = operation;
  perf_data["model"] = "Long-CLIP";

  perf_data["image_delay_us"] = image_delay_us;
  perf_data["preprocess_us"] = preprocess_us;
  perf_data["inference_us"] = inference_us;
  perf_data["post_process_us"] = post_process_us;
  perf_data["cvflow_us"] = cvflow_us;

  auto perf_msg = std_msgs::msg::String();
  perf_msg.data = perf_data.dump();
  performance_publisher_->publish(perf_msg);
}

}  // namespace clip
}  // namespace cooper_ros

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(cooper_ros::clip::ClipNode)
