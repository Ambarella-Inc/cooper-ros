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

// Local headers
#include "cooper_ros_clip/clip_net.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <sys/stat.h>

#include <eazyai.h>
#include <eazyai_inf.h>
#include <eazyai_io.h>
#include <eazyai_postprocess.h>

// Third-party library headers
#include <opencv2/opencv.hpp>
#include <tokenizers/tokenizers_cpp.h>

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

/** @brief Tensor shape array size (NCHW format) */
#define TENSOR_SHAPE_SIZE 4

/**
 * @brief CLIP tokenizer structure for internal use
 */
typedef struct clip_tokenizer_s
{
  uint8_t enable_adaptive_truncation;  ///< Tokenizer adaptive truncation flag
  uint8_t enable_custom_padding;       ///< Fill the tokenizer with a custom ID flag
  uint16_t padding_id;                 ///< Customize tokenizer padding ID
  char * token_cls;          ///< Token classification string at the beginning of the sentence
  char * token_sep;          ///< Token separator string at the end of the sentence
  uint32_t in_token_maxlen;  ///< Maximum input token length
  uint32_t batch;            ///< The number of the in_text array
  uint32_t pitch;            ///< The out_token_id actual memory size in bytes of each W
} clip_tokenizer_t;

/**
 * @brief CLIP execution context containing all runtime data
 * @note This is the internal implementation of clip_context_t
 */
struct clip_context_s
{
  // Core execution parameters
  clip_mode_t mode;     ///< CLIP execution mode
  uint32_t top_k;       ///< Number of top results to return
  uint32_t batch_num;   ///< Batch size for processing
  uint8_t paddingcrop;  ///< Padding/crop mode flag

  // Path strings (static data)
  std::string image_path;        ///< Single image path (CLASSIFICATION mode)
  std::string query_image_path;  ///< Query image path (SEARCH mode)

  // Data arrays and counts (dynamic data)
  std::unique_ptr<char[]> text;                  ///< Text array (RETRIEVAL mode)
  std::vector<std::unique_ptr<char[]>> images;   ///< Image array (RETRIEVAL and SEARCH modes)
  std::vector<std::unique_ptr<char[]>> prompts;  ///< Prompts array (CLASSIFICATION mode)
  uint32_t images_count;                         ///< Number of images (RETRIEVAL and SEARCH modes)
  uint32_t prompt_count;                         ///< Number of prompts (CLASSIFICATION mode)

  // Model instances (core objects)
  ea_clip_image_enc_t * clip_image;  ///< Image encoder model instance
  ea_clip_text_enc_t * clip_text;    ///< Text encoder model instance

  // Input/Output ports (model interfaces)
  ea_clip_port_t * clip_image_in;   ///< Image encoder input port
  ea_clip_port_t * clip_image_out;  ///< Image encoder output port
  ea_clip_port_t * clip_text_in;    ///< Text encoder input port
  ea_clip_port_t * clip_text_out;   ///< Text encoder output port

  // Configuration and results (runtime data)
  clip_tokenizer_t clip_tokenizer;                           ///< Tokenizer configuration
  std::unique_ptr<tokenizers::Tokenizer> hf_tokenizer;       ///< HuggingFace tokenizer instance
  std::unique_ptr<confidence_levels_t[]> confidence_levels;  ///< Inference results array
  clip_performance_t clip_performance;                       ///< Performance metrics
};

static int clip_check_file_exists(const char * path)
{
  struct stat st;
  return (stat(path, &st) == 0) && S_ISREG(st.st_mode) ? 0 : -1;
}

static std::string clip_read_hf_tokenizer_json(const std::string & tokenizer_json_path)
{
  if (clip_check_file_exists(tokenizer_json_path.c_str()) < 0) {
    EA_LOG_ERROR("File does not exist: %s\n", tokenizer_json_path.c_str());
    return {};
  }

  std::ifstream fs(tokenizer_json_path, std::ios::in | std::ios::binary);
  if (!fs.is_open()) {
    EA_LOG_ERROR("Cannot open %s\n", tokenizer_json_path.c_str());
    return {};
  }

  fs.seekg(0, std::ios::end);
  const auto size = fs.tellg();
  fs.seekg(0, std::ios::beg);

  if (size <= 0) {
    EA_LOG_ERROR("Invalid file size for %s\n", tokenizer_json_path.c_str());
    return {};
  }

  std::string data(static_cast<size_t>(size), '\0');
  fs.read(data.data(), size);

  if (fs.gcount() != size) {
    EA_LOG_ERROR("Failed to read complete file content from %s\n", tokenizer_json_path.c_str());
    return {};
  }

  return data;
}

static std::unique_ptr<tokenizers::Tokenizer> clip_hf_tokenizer_new(
  const char * tokenizer_path_path)
{
  if (tokenizer_path_path == nullptr) {
    EA_LOG_ERROR("tokenizer_path_path is null\n");
    return nullptr;
  }

  std::string json_str = clip_read_hf_tokenizer_json(tokenizer_path_path);
  if (json_str.empty()) {
    EA_LOG_ERROR("Failed to read tokenizer file content from: %s\n", tokenizer_path_path);
    return nullptr;
  }

  try {
    return std::unique_ptr<tokenizers::Tokenizer>(tokenizers::Tokenizer::FromBlobJSON(json_str));
  } catch (const std::exception & e) {
    EA_LOG_ERROR(
      "Failed to create tokenizer from JSON file %s: %s\n",
      tokenizer_path_path,
      e.what());
    return nullptr;
  }
}

static int clip_init_port(ea_clip_port_t ** port, const ea_clip_port_msg_t * info)
{
  int rval = 0;

  do {
    *port = new ea_clip_port_t();
    EA_R_ASSERT(*port != nullptr);

    memset(*port, 0, sizeof(ea_clip_port_t));
    snprintf((*port)->port_name, sizeof((*port)->port_name), "%s", info->name);
    (*port)->port = ea_tensor_new(info->dtype, info->shape, info->pitch);
    if ((*port)->port == nullptr) {
      delete *port;
      *port = nullptr;
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static void clip_free_port(ea_clip_port_t ** port)
{
  if (*port != nullptr) {
    if ((*port)->port != nullptr) {
      ea_tensor_free((*port)->port);
    }
    delete *port;
    *port = nullptr;
  }
}

static int clip_init_image_encoder(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;

  do {
    ea_clip_image_params_t clip_image_params = {};
    clip_image_params.log_level = params->log_level;
    clip_image_params.model_path = params->image_model_path;
    clip_image_params.max_batch_num =
      (params->mode == CLIP_MODE_CLASSIFICATION) ? 1 : params->batch_num;
    clip_image_params.nvp_affinity = params->nvp_affinity;
    clip_image_params.disable_output_fp16_2_fp32 = 0;

    clip_ctx->clip_image = ea_clip_image_enc_init(&clip_image_params);
    EA_R_ASSERT(clip_ctx->clip_image != nullptr);

    if (clip_init_port(&clip_ctx->clip_image_in, clip_ctx->clip_image->in_info) < 0) {
      rval = -1;
      break;
    }

    if (clip_init_port(&clip_ctx->clip_image_out, clip_ctx->clip_image->out_info) < 0) {
      if (clip_ctx->clip_image_in != nullptr) {
        if (clip_ctx->clip_image_in->port != nullptr) {
          ea_tensor_free(clip_ctx->clip_image_in->port);
        }
        delete clip_ctx->clip_image_in;
        clip_ctx->clip_image_in = nullptr;
      }
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static int clip_init_text_encoder(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;
  ea_clip_port_t * text_in_port = nullptr;
  ea_clip_port_t * text_out_port = nullptr;

  do {
    ea_clip_text_params_t clip_text_params = {};
    clip_text_params.log_level = params->log_level;
    clip_text_params.model_path = params->text_model_path;
    clip_text_params.token_embedded_weight_path = params->text_embedded_weight_path;
    clip_text_params.max_batch_num =
      (params->mode == CLIP_MODE_CLASSIFICATION) ? params->batch_num : 1;
    clip_text_params.nvp_affinity = params->nvp_affinity;
    clip_text_params.disable_output_fp16_2_fp32 = 0;

    clip_ctx->clip_text = ea_clip_text_enc_init(&clip_text_params);
    EA_R_ASSERT(clip_ctx->clip_text != nullptr);

    if (clip_init_port(&text_in_port, clip_ctx->clip_text->in_info) < 0) {
      EA_LOG_ERROR("Failed to initialize text encoder input port\n");
      rval = -1;
      break;
    }

    if (clip_init_port(&text_out_port, clip_ctx->clip_text->out_info) < 0) {
      EA_LOG_ERROR("Failed to initialize text encoder output port\n");
      rval = -1;
      break;
    }

    EA_R_OK(
      ea_tensor_set_data_format(text_in_port->port, &clip_ctx->clip_text->in_info->data_format));

    clip_ctx->hf_tokenizer = clip_hf_tokenizer_new(params->vocab_path);
    if (clip_ctx->hf_tokenizer == nullptr) {
      EA_LOG_ERROR("Failed to initialize HuggingFace tokenizer from: %s\n", params->vocab_path);
      rval = -1;
      break;
    }

    clip_ctx->clip_text_in = text_in_port;
    clip_ctx->clip_text_out = text_out_port;
    text_in_port = nullptr;
    text_out_port = nullptr;

  } while (0);

  if (rval < 0) {
    if (text_in_port != nullptr) {
      clip_free_port(&text_in_port);
    }
    if (text_out_port != nullptr) {
      clip_free_port(&text_out_port);
    }
    if (clip_ctx->clip_text != nullptr) {
      ea_clip_text_enc_deinit(clip_ctx->clip_text);
      clip_ctx->clip_text = nullptr;
    }
  }

  return rval;
}

static std::vector<std::string> clip_search_image_files(const std::string & directory)
{
  std::vector<std::string> image_files;
  const std::vector<std::string> extensions = {".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif"};

  try {
    for (const auto & entry : std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        for (const auto & valid_ext : extensions) {
          if (ext == valid_ext) {
            image_files.push_back(entry.path().string());
            break;
          }
        }
      }
    }
  } catch (const std::exception & e) {
    EA_LOG_ERROR("Error reading directory %s: %s", directory.c_str(), e.what());
  }

  return image_files;
}

static int clip_load_image_list(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;
  std::vector<std::string> image_files;
  std::string line;

  do {
    image_files = clip_search_image_files(params->images_dir);

    if (image_files.empty()) {
      EA_LOG_ERROR("No valid images found.\n");
      rval = -1;
      break;
    }

    clip_ctx->images_count = static_cast<uint32_t>(image_files.size());
    clip_ctx->images.clear();
    clip_ctx->images.reserve(clip_ctx->images_count);

    for (uint32_t i = 0; i < clip_ctx->images_count; ++i) {
      size_t len = image_files[i].length() + 1;
      auto image_ptr = std::make_unique<char[]>(len);
      std::copy(image_files[i].begin(), image_files[i].end(), image_ptr.get());
      image_ptr.get()[image_files[i].length()] = '\0';
      clip_ctx->images.push_back(std::move(image_ptr));
      EA_LOG_DEBUG("image[%d] path is: %s\n", i, clip_ctx->images[i].get());
    }

    EA_LOG_DEBUG("total images numbers: %d\n", clip_ctx->images_count);
    if (EA_LOG_GET_LOCAL() == EA_LOG_LEVEL_DEBUG) {
      for (uint32_t i = 0; i < clip_ctx->images_count; i++) {
        EA_LOG_DEBUG("prompt: %s\n", clip_ctx->images[i].get());
      }
    }
  } while (0);

  return rval;
}

static void clip_free_image_list(clip_context_t * clip_ctx)
{
  clip_ctx->images.clear();
  clip_ctx->confidence_levels.reset();
}

static int clip_load_prompt_list(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;
  std::vector<std::string> valid_prompts;
  std::string line;

  do {
    std::ifstream file(params->prompts_path);
    if (!file.is_open()) {
      EA_LOG_ERROR("Failed to open prompts file: %s\n", params->prompts_path);
      rval = -1;
      break;
    }

    while (std::getline(file, line)) {
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

      if (!line.empty()) {
        valid_prompts.push_back(line);
      }
    }
    file.close();

    if (valid_prompts.empty()) {
      EA_LOG_ERROR("No valid prompts found.\n");
      rval = -1;
      break;
    }

    clip_ctx->prompt_count = static_cast<uint32_t>(valid_prompts.size());
    clip_ctx->prompts.clear();
    clip_ctx->prompts.reserve(clip_ctx->prompt_count);

    for (uint32_t i = 0; i < clip_ctx->prompt_count; ++i) {
      size_t len = valid_prompts[i].length() + 1;
      auto prompt_ptr = std::make_unique<char[]>(len);
      std::copy(valid_prompts[i].begin(), valid_prompts[i].end(), prompt_ptr.get());
      prompt_ptr.get()[valid_prompts[i].length()] = '\0';
      clip_ctx->prompts.push_back(std::move(prompt_ptr));
    }

    EA_LOG_DEBUG("total prompt numbers: %d\n", clip_ctx->prompt_count);

    if (EA_LOG_GET_LOCAL() == EA_LOG_LEVEL_DEBUG) {
      EA_LOG_DEBUG("prompt: \n");
      for (uint32_t i = 0; i < clip_ctx->prompt_count; i++) {
        EA_LOG_DEBUG("%s ", clip_ctx->prompts[i].get());
      }
      EA_LOG_DEBUG("\n");
    }
  } while (0);

  if (rval < 0) {
    clip_ctx->prompts.clear();
  }

  return rval;
}

static void clip_free_prompt_list(clip_context_t * clip_ctx)
{
  clip_ctx->prompts.clear();
  clip_ctx->confidence_levels.reset();
}

static void clip_free_text(clip_context_t * clip_ctx)
{
  clip_ctx->text.reset();
}

static int clip_init_text_data(clip_context_t * clip_ctx, const char * text)
{
  int rval = 0;

  do {
    EA_R_ASSERT(text != nullptr);

    size_t text_len = strlen(text) + 1;
    clip_ctx->text = std::make_unique<char[]>(text_len);
    EA_R_ASSERT(clip_ctx->text != nullptr);
    std::copy(text, text + strlen(text), clip_ctx->text.get());
    clip_ctx->text.get()[strlen(text)] = '\0';
  } while (0);

  return rval;
}

static int clip_confidence_levels_new(clip_context_t * clip_ctx, uint32_t count)
{
  int rval = 0;

  do {
    if (clip_ctx->top_k > count) {
      EA_LOG_ERROR("top_k (%d) should be <= count (%d)\n", clip_ctx->top_k, count);
      rval = -1;
      break;
    }

    clip_ctx->confidence_levels = std::make_unique<confidence_levels_t[]>(count);
    EA_R_ASSERT(clip_ctx->confidence_levels != nullptr);
  } while (0);

  return rval;
}

static int clip_init_retrieval_mode_data(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;

  do {
    if (clip_init_text_data(clip_ctx, params->text) < 0) {
      rval = -1;
      break;
    }

    if (clip_load_image_list(clip_ctx, params) < 0) {
      rval = -1;
      break;
    }

    if (clip_confidence_levels_new(clip_ctx, clip_ctx->images_count) < 0) {
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static int clip_init_classification_mode_data(
  clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;

  do {
    clip_ctx->image_path = params->image_path;

    if (clip_load_prompt_list(clip_ctx, params) < 0) {
      rval = -1;
      break;
    }

    if (clip_confidence_levels_new(clip_ctx, clip_ctx->prompt_count) < 0) {
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static int clip_init_search_mode_data(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;

  do {
    clip_ctx->query_image_path = params->query_image_path;

    if (clip_load_image_list(clip_ctx, params) < 0) {
      rval = -1;
      break;
    }

    if (clip_confidence_levels_new(clip_ctx, clip_ctx->images_count) < 0) {
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static int clip_init_mode_data(clip_context_t * clip_ctx, const clip_params_t * params)
{
  int rval = 0;

  switch (params->mode) {
    case CLIP_MODE_RETRIEVAL:
      rval = clip_init_retrieval_mode_data(clip_ctx, params);
      break;

    case CLIP_MODE_CLASSIFICATION:
      rval = clip_init_classification_mode_data(clip_ctx, params);
      break;

    case CLIP_MODE_SEARCH:
      rval = clip_init_search_mode_data(clip_ctx, params);
      break;

    default:
      EA_LOG_ERROR("Unknown mode: %d\n", (int)(params->mode));
      rval = -1;
      break;
  }

  return rval;
}

static int clip_hf_tokenize_encode(
  const std::unique_ptr<tokenizers::Tokenizer> & hf_tokenizer,
  clip_tokenizer_t * clip_tokenizer,
  const char ** in_text,
  uint16_t * out_token_id)
{
  int rval = 0;
  int last_nonzero_value = 0;
  std::string full_prompt;
  std::vector<std::string> text_sets;
  std::vector<int> token_ids_sets;
  std::vector<std::vector<int>> token_ids;

  do {
    EA_R_ASSERT(clip_tokenizer != nullptr);
    EA_R_ASSERT(in_text != nullptr);
    EA_R_ASSERT(out_token_id != nullptr);

    if (clip_tokenizer->batch == 0) {
      clip_tokenizer->batch = 1;
    }
    if (clip_tokenizer->pitch == 0) {
      clip_tokenizer->pitch = 1;
    }
    for (int i = 0; i < (int)clip_tokenizer->batch; i++) {
      if (clip_tokenizer->token_cls != nullptr || clip_tokenizer->token_sep != nullptr) {
        full_prompt = std::string(clip_tokenizer->token_cls) + std::string(in_text[i]) +
                      std::string(clip_tokenizer->token_sep);
      } else {
        full_prompt = std::string(in_text[i]);
      }
      text_sets.push_back(full_prompt);
    }
    token_ids.resize(text_sets.size());
    for (size_t i = 0; i < text_sets.size(); i++) {
      token_ids[i] = hf_tokenizer->Encode(text_sets[i]);
    }

    if (token_ids.size() * clip_tokenizer->in_token_maxlen > token_ids_sets.size()) {
      token_ids_sets.resize(token_ids.size() * clip_tokenizer->in_token_maxlen);
    }
    memset(
      token_ids_sets.data(),
      0,
      token_ids.size() * clip_tokenizer->in_token_maxlen * sizeof(int));
    auto token_ids_sets_input_ptr = token_ids_sets.data();
    for (size_t i = 0; i < token_ids.size(); i++) {
      if (token_ids[i].size() > clip_tokenizer->in_token_maxlen) {
        if (clip_tokenizer->enable_adaptive_truncation) {
          EA_LOG_NOTICE(
            "input prompt is too long, will truncate it to %d length\n",
            clip_tokenizer->in_token_maxlen);
          token_ids[i].resize(clip_tokenizer->in_token_maxlen);
          token_ids[i][clip_tokenizer->in_token_maxlen - 1] =
            hf_tokenizer->Encode(clip_tokenizer->token_sep)[0];
        } else {
          EA_LOG_ERROR("token_id index %ld ,bigger than %d\n", i, clip_tokenizer->in_token_maxlen);
          rval = -1;
          break;
        }
      } else {
        token_ids[i].resize(clip_tokenizer->in_token_maxlen);
        if (clip_tokenizer->enable_custom_padding) {
          for (uint32_t start = 0; start < token_ids[i].size();
               start += clip_tokenizer->in_token_maxlen) {
            auto it = std::find(
              token_ids[i].begin() + start,
              token_ids[i].begin() +
                std::min(start + clip_tokenizer->in_token_maxlen, (uint32_t)token_ids[i].size()),
              last_nonzero_value);
            if (
              it !=
              token_ids[i].begin() +
                std::min(start + clip_tokenizer->in_token_maxlen, (uint32_t)token_ids[i].size())) {
              std::fill(
                std::next(it),
                token_ids[i].begin() +
                  std::min(start + clip_tokenizer->in_token_maxlen, (uint32_t)token_ids[i].size()),
                (int)clip_tokenizer->padding_id);
            }
          }
        }
      }
      memcpy(
        token_ids_sets_input_ptr + i * clip_tokenizer->in_token_maxlen,
        token_ids[i].data(),
        clip_tokenizer->in_token_maxlen * sizeof(int));
    }
    EA_R_BREAK();

    std::vector<uint16_t> token_ids_sets_uint16(token_ids_sets.begin(), token_ids_sets.end());
    auto token_ids_sets_uint16_ptr = token_ids_sets_uint16.data();
    for (int i = 0; i < (int)clip_tokenizer->batch; i++) {
      memcpy(
        out_token_id + i * clip_tokenizer->pitch / sizeof(uint16_t),
        token_ids_sets_uint16_ptr + i * clip_tokenizer->in_token_maxlen,
        clip_tokenizer->in_token_maxlen * sizeof(uint16_t));
    }
  } while (0);

  return rval;
}

static int clip_preprocess_image_crop(const char * image_path, ea_tensor_t * dst)
{
  int rval = 0;
  ea_tensor_t * image_tensor = nullptr;
  ea_tensor_t * cropped_tensor = nullptr;

  do {
    EA_R_ASSERT(image_path != nullptr);
    EA_R_ASSERT(dst != nullptr);

    image_tensor = ea_tensor_new_from_image(image_path);
    EA_R_ASSERT(image_tensor != nullptr);

    const size_t * image_shape = ea_tensor_shape(image_tensor);
    EA_R_ASSERT(image_shape != nullptr);

    int image_height = image_shape[EA_H];
    int image_width = image_shape[EA_W];
    int crop_size = (image_width < image_height) ? image_width : image_height;

    ea_roi_t crop_roi = {0, 0, 0, 0};
    crop_roi.w = crop_size;
    crop_roi.h = crop_size;
    crop_roi.x = (image_width - crop_size) / 2;
    crop_roi.y = (image_height - crop_size) / 2;

    EA_LOG_DEBUG(
      "Crop size: x: %d y: %d w: %d h: %d\n",
      crop_roi.x,
      crop_roi.y,
      crop_roi.w,
      crop_roi.h);

    size_t crop_shape[TENSOR_SHAPE_SIZE] = {
      image_shape[EA_N],
      image_shape[EA_C],
      (size_t)crop_roi.h,
      (size_t)crop_roi.w};

    cropped_tensor = ea_tensor_new(ea_tensor_dtype(image_tensor), crop_shape, 0);
    EA_R_ASSERT(cropped_tensor != nullptr);

    EA_R_OK(ea_crop_resize(
      &image_tensor,
      1,
      &cropped_tensor,
      1,
      &crop_roi,
      EA_TENSOR_COLOR_MODE_BGR,
      EA_VP));
    EA_R_OK(ea_cvt_color_resize(cropped_tensor, dst, EA_COLOR_BGR2RGB, EA_VP));
  } while (0);

  if (image_tensor != nullptr) {
    ea_tensor_free(image_tensor);
    image_tensor = nullptr;
  }
  if (cropped_tensor != nullptr) {
    ea_tensor_free(cropped_tensor);
    cropped_tensor = nullptr;
  }

  return rval;
}

static int clip_preprocess_image_pad(const char * image_path, ea_tensor_t * dst)
{
  int rval = EA_SUCCESS;
  ea_tensor_t * padded_tensor = nullptr;

  do {
    EA_R_ASSERT(image_path != nullptr);
    EA_R_ASSERT(dst != nullptr);

    cv::Mat input_img = cv::imread(image_path);
    if (input_img.empty()) {
      EA_LOG_ERROR("Error loading image: %s\n", image_path);
      rval = -1;
      break;
    }

    int max_side = std::max(input_img.cols, input_img.rows);
    size_t shape[TENSOR_SHAPE_SIZE] = {1, 3, (size_t)max_side, (size_t)max_side};
    padded_tensor = ea_tensor_new(EA_U8, shape, 0);
    EA_R_ASSERT(padded_tensor);

    int top = (max_side - input_img.rows) / 2;
    int bottom = max_side - input_img.rows - top;
    int left = (max_side - input_img.cols) / 2;
    int right = max_side - input_img.cols - left;

    cv::Mat padded_image;
    cv::copyMakeBorder(input_img, padded_image, top, bottom, left, right, cv::BORDER_REPLICATE);

    uint8_t * padded_data = (uint8_t *)ea_tensor_data(padded_tensor);
    EA_R_ASSERT(padded_data);

    std::vector<cv::Mat> channels;
    cv::split(padded_image, channels);
    int channel_size = padded_image.rows * padded_image.cols;
    for (int c = 0; c < 3; ++c) {
      memcpy(padded_data + c * channel_size, channels[c].data, channel_size);
    }

    EA_R_OK(ea_cvt_color_resize(padded_tensor, dst, EA_COLOR_BGR2RGB, EA_VP));

    if (padded_tensor) {
      ea_tensor_free(padded_tensor);
      padded_tensor = nullptr;
    }
  } while (0);

  if (rval < 0 && padded_tensor) {
    ea_tensor_free(padded_tensor);
    padded_tensor = nullptr;
  }

  return rval;
}

COOPER_CLIP_API clip_context_t * clip_context_init(const clip_params_t * const params)
{
  int rval = 0;
  clip_context_t * clip_ctx = nullptr;

  do {
    EA_R_ASSERT(params != nullptr);

    clip_ctx = new clip_context_t();
    EA_R_ASSERT(clip_ctx != nullptr);

    clip_ctx->mode = params->mode;
    clip_ctx->top_k = params->top_k;
    clip_ctx->batch_num = params->batch_num;
    clip_ctx->paddingcrop = params->paddingcrop;

    if (clip_init_mode_data(clip_ctx, params) < 0) {
      rval = -1;
      break;
    }

    if (clip_init_image_encoder(clip_ctx, params) < 0) {
      rval = -1;
      break;
    }

    if (params->mode == CLIP_MODE_RETRIEVAL || params->mode == CLIP_MODE_CLASSIFICATION) {
      if (clip_init_text_encoder(clip_ctx, params) < 0) {
        rval = -1;
        break;
      }
    }
  } while (0);

  if (rval < 0) {
    if (clip_ctx != nullptr) {
      clip_context_deinit(clip_ctx);
      clip_ctx = nullptr;
    }
  }

  return clip_ctx;
}

COOPER_CLIP_API void clip_context_deinit(clip_context_t * const clip_ctx)
{
  if (clip_ctx->clip_text != nullptr) {
    ea_clip_text_enc_deinit(clip_ctx->clip_text);
    clip_ctx->clip_text = nullptr;
  }
  clip_free_port(&clip_ctx->clip_text_in);
  clip_free_port(&clip_ctx->clip_text_out);

  if (clip_ctx->clip_image != nullptr) {
    ea_clip_image_enc_deinit(clip_ctx->clip_image);
    clip_ctx->clip_image = nullptr;
  }
  clip_free_port(&clip_ctx->clip_image_in);
  clip_free_port(&clip_ctx->clip_image_out);

  switch (clip_ctx->mode) {
    case CLIP_MODE_RETRIEVAL:
      clip_free_text(clip_ctx);
      clip_free_image_list(clip_ctx);
      break;
    case CLIP_MODE_SEARCH:
      clip_free_image_list(clip_ctx);
      break;
    case CLIP_MODE_CLASSIFICATION:
      clip_free_prompt_list(clip_ctx);
      break;
    default:
      break;
  }
  delete clip_ctx;
}

COOPER_CLIP_API int clip_run_text_to_image_retrieval(clip_context_t * const clip_ctx)
{
  int rval = EA_SUCCESS;
  int result_index = 0;
  int unprocessed_image_count = 0;
  uint32_t batch_index = 0;
  ea_clip_image_enc_t * clip_image_net = clip_ctx->clip_image;
  ea_clip_text_enc_t * clip_text_net = clip_ctx->clip_text;
  uint16_t * tokens_data = nullptr;
  ea_tensor_t * tmp_batch_tensor = nullptr;
  float text_sorces[clip_ctx->images_count];
  ea_postproc_tensor_t base_tensor;
  ea_postproc_tensor_t image_tensor;
  ea_postproc_tensor_t logits_tensor;
  std::unique_ptr<float[]> logits_buffer;
  std::string cls = "<|startoftext|>";
  std::string seq = "<|endoftext|>";

  do {
    EA_R_ASSERT(clip_ctx != nullptr);
    for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
      logits_tensor.shape[m] = 1;
    }
    logits_tensor.pitch = 0;
    logits_tensor.data_format = EA_P_F32;
    logits_buffer = std::make_unique<float[]>(logits_tensor.shape[2] * logits_tensor.shape[3]);
    EA_R_ASSERT(logits_buffer != nullptr);
    logits_tensor.p_buffer = logits_buffer.get();

    tokens_data = (uint16_t *)ea_tensor_data(clip_ctx->clip_text_in->port);
    EA_R_ASSERT(tokens_data != nullptr);
    clip_ctx->clip_tokenizer.enable_adaptive_truncation = 1;
    clip_ctx->clip_tokenizer.in_token_maxlen = clip_ctx->clip_text->in_info->shape[EA_W];
    clip_ctx->clip_tokenizer.token_cls = const_cast<char *>(cls.c_str());
    clip_ctx->clip_tokenizer.token_sep = const_cast<char *>(seq.c_str());
    clip_ctx->clip_tokenizer.pitch = clip_ctx->clip_text->in_info->pitch;
    clip_ctx->clip_tokenizer.batch = 1;
    const char * text_ptr = clip_ctx->text.get();
    EA_R_OK(clip_hf_tokenize_encode(
      clip_ctx->hf_tokenizer,
      &clip_ctx->clip_tokenizer,
      &text_ptr,
      tokens_data));

    clip_ctx->clip_performance.inference_start_time_us = ea_gettime_us();

    EA_R_OK(
      ea_clip_text_enc_inf(clip_text_net, clip_ctx->clip_text_in, clip_ctx->clip_text_out, 1));
    EA_LOG_NOTICE("clip text encoder VP times: %lu us\n", clip_text_net->inf_vp_time);

    clip_ctx->clip_performance.cvflow_us += clip_text_net->inf_vp_time;

    for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
      base_tensor.shape[m] = ea_tensor_shape(clip_ctx->clip_text_out->port)[m];
    }
    base_tensor.p_buffer = (uint8_t *)ea_tensor_data(clip_ctx->clip_text_out->port);
    base_tensor.data_format = EA_P_F32;
    base_tensor.pitch = ea_tensor_pitch(clip_ctx->clip_text_out->port);

    EA_R_OK(ea_normalization_by_norm(&base_tensor, &base_tensor, EA_P_NORM_L2));

    unprocessed_image_count = clip_ctx->images_count;
    for (uint32_t i = 0; i < clip_ctx->images_count; i++) {
      tmp_batch_tensor =
        ea_tensor_new_from_other_sub(clip_ctx->clip_image_in->port, batch_index, batch_index + 1);
      if (clip_ctx->paddingcrop) {
        EA_R_OK(clip_preprocess_image_pad(clip_ctx->images[i].get(), tmp_batch_tensor));
      } else {
        EA_R_OK(clip_preprocess_image_crop(clip_ctx->images[i].get(), tmp_batch_tensor));
      }

      if (tmp_batch_tensor != nullptr) {
        ea_tensor_free(tmp_batch_tensor);
        tmp_batch_tensor = nullptr;
      }

      batch_index++;
      unprocessed_image_count--;
      if ((batch_index == clip_ctx->batch_num) || (unprocessed_image_count == 0)) {
        EA_R_OK(ea_clip_image_enc_inf(
          clip_image_net,
          clip_ctx->clip_image_in,
          clip_ctx->clip_image_out,
          batch_index));
        EA_LOG_NOTICE("clip image encoder VP times: %lu us\n", clip_image_net->inf_vp_time);

        clip_ctx->clip_performance.cvflow_us += clip_image_net->inf_vp_time;
      }
      if ((batch_index < clip_ctx->batch_num) && (unprocessed_image_count != 0)) {
        continue;
      }

      for (int k = 0; k < (int)batch_index; k++) {
        tmp_batch_tensor = ea_tensor_new_from_other_sub(clip_ctx->clip_image_out->port, k, k + 1);
        EA_R_ASSERT(tmp_batch_tensor != nullptr);
        for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
          image_tensor.shape[m] = ea_tensor_shape(tmp_batch_tensor)[m];
        }
        image_tensor.p_buffer = (uint8_t *)ea_tensor_data(tmp_batch_tensor);
        image_tensor.data_format = EA_P_F32;
        image_tensor.pitch = ea_tensor_pitch(tmp_batch_tensor);
        EA_R_OK(ea_normalization_by_norm(&image_tensor, &image_tensor, EA_P_NORM_L2));

        EA_R_OK(ea_matmul(&base_tensor, &image_tensor, &logits_tensor, 100.0f, 0, 1));

        memset(&clip_ctx->confidence_levels.get()[result_index], 0, sizeof(confidence_levels_t));
        text_sorces[result_index] = ((float *)logits_tensor.p_buffer)[0];

        snprintf(
          clip_ctx->confidence_levels.get()[result_index].image_info,
          CLIP_PATH_MAX_LEN,
          "%s",
          clip_ctx->images[result_index].get());
        result_index++;
        if (tmp_batch_tensor != nullptr) {
          ea_tensor_free(tmp_batch_tensor);
          tmp_batch_tensor = nullptr;
        }
      }
      EA_R_BREAK();
      batch_index = 0;
    }
    EA_R_BREAK();

    for (uint32_t i = 0; i < clip_ctx->images_count; ++i) {
      clip_ctx->confidence_levels.get()[i].sorce = text_sorces[i];
    }

    EA_R_OK(ea_softmax(text_sorces, clip_ctx->images_count, text_sorces));
    clip_ctx->clip_performance.inference_end_time_us = ea_gettime_us();
    clip_ctx->clip_performance.inference_us = clip_ctx->clip_performance.inference_end_time_us -
                                              clip_ctx->clip_performance.inference_start_time_us;

    for (uint32_t i = 0; i < clip_ctx->images_count; ++i) {
      clip_ctx->confidence_levels.get()[i].logits = text_sorces[i];
    }

    for (uint32_t i = 0; i < clip_ctx->images_count; ++i) {
      for (uint32_t j = 0; j < clip_ctx->images_count - i - 1; ++j) {
        if (
          clip_ctx->confidence_levels.get()[j].logits <
          clip_ctx->confidence_levels.get()[j + 1].logits) {
          confidence_levels_t temp = clip_ctx->confidence_levels.get()[j];
          clip_ctx->confidence_levels.get()[j] = clip_ctx->confidence_levels.get()[j + 1];
          clip_ctx->confidence_levels.get()[j + 1] = temp;
        }
      }
    }

    if (clip_ctx->top_k > clip_ctx->images_count) {
      EA_LOG_ERROR(
        "The top_k: %d should be less than or equal to the number of images: %d.\n",
        clip_ctx->top_k,
        clip_ctx->images_count);
      rval = -1;
      break;
    }
  } while (0);

  if (rval < 0) {
    if (tmp_batch_tensor != nullptr) {
      ea_tensor_free(tmp_batch_tensor);
      tmp_batch_tensor = nullptr;
    }
  }

  return rval;
}

COOPER_CLIP_API int clip_run_image_classification(clip_context_t * const clip_ctx)
{
  int rval = EA_SUCCESS;
  int result_index = 0;
  int unprocessed_prompt_count = 0;
  uint32_t batch_index = 0;
  ea_clip_image_enc_t * clip_image_net = clip_ctx->clip_image;
  ea_clip_text_enc_t * clip_text_net = clip_ctx->clip_text;
  ea_tensor_t * tmp_batch_tensor = nullptr;
  uint16_t * tokens_data = nullptr;
  std::unique_ptr<char *[]> tmp_prompts;
  float text_sorces[clip_ctx->prompt_count];
  ea_postproc_tensor_t base_tensor;
  ea_postproc_tensor_t prompt_tensor;
  ea_postproc_tensor_t logits_tensor;
  std::unique_ptr<float[]> logits_buffer;
  std::string cls = "<|startoftext|>";
  std::string seq = "<|endoftext|>";

  do {
    EA_R_ASSERT(clip_ctx != nullptr);
    for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
      logits_tensor.shape[m] = 1;
    }
    logits_tensor.pitch = 0;
    logits_tensor.data_format = EA_P_F32;
    logits_buffer = std::make_unique<float[]>(logits_tensor.shape[2] * logits_tensor.shape[3]);
    EA_R_ASSERT(logits_buffer != nullptr);
    logits_tensor.p_buffer = logits_buffer.get();

    if ((clip_check_file_exists(clip_ctx->image_path.c_str()) < 0)) {
      EA_LOG_ERROR(
        "The image path: <%s> is not a file path, please enter a file path.\n",
        clip_ctx->image_path.c_str());
      rval = -1;
      break;
    }
    if (clip_ctx->paddingcrop) {
      EA_R_OK(
        clip_preprocess_image_pad(clip_ctx->image_path.c_str(), clip_ctx->clip_image_in->port));
    } else {
      EA_R_OK(
        clip_preprocess_image_crop(clip_ctx->image_path.c_str(), clip_ctx->clip_image_in->port));
    }

    clip_ctx->clip_performance.inference_start_time_us = ea_gettime_us();

    EA_R_OK(
      ea_clip_image_enc_inf(clip_image_net, clip_ctx->clip_image_in, clip_ctx->clip_image_out, 1));
    EA_LOG_NOTICE("clip image encoder VP times: %lu us\n", clip_image_net->inf_vp_time);

    clip_ctx->clip_performance.cvflow_us += clip_image_net->inf_vp_time;

    for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
      base_tensor.shape[m] = ea_tensor_shape(clip_ctx->clip_image_out->port)[m];
    }
    base_tensor.p_buffer = (uint8_t *)ea_tensor_data(clip_ctx->clip_image_out->port);
    base_tensor.data_format = EA_P_F32;
    base_tensor.pitch = ea_tensor_pitch(clip_ctx->clip_image_out->port);

    EA_R_OK(ea_normalization_by_norm(&base_tensor, &base_tensor, EA_P_NORM_L2));

    unprocessed_prompt_count = clip_ctx->prompt_count;
    tokens_data = (uint16_t *)ea_tensor_data(clip_ctx->clip_text_in->port);
    EA_R_ASSERT(tokens_data != nullptr);
    tmp_prompts = std::make_unique<char *[]>(clip_ctx->batch_num);
    EA_R_ASSERT(tmp_prompts != nullptr);

    clip_ctx->clip_tokenizer.enable_adaptive_truncation = 1;
    clip_ctx->clip_tokenizer.in_token_maxlen = clip_ctx->clip_text->in_info->shape[EA_W];
    clip_ctx->clip_tokenizer.token_cls = const_cast<char *>(cls.c_str());
    clip_ctx->clip_tokenizer.token_sep = const_cast<char *>(seq.c_str());
    clip_ctx->clip_tokenizer.pitch = clip_ctx->clip_text->in_info->pitch;
    for (uint32_t i = 0; i < clip_ctx->prompt_count; i++) {
      tmp_prompts[batch_index] = clip_ctx->prompts[i].get();

      batch_index++;
      unprocessed_prompt_count--;

      if ((batch_index == clip_ctx->batch_num) || (unprocessed_prompt_count == 0)) {
        clip_ctx->clip_tokenizer.batch = batch_index;
        EA_R_OK(clip_hf_tokenize_encode(
          clip_ctx->hf_tokenizer,
          &clip_ctx->clip_tokenizer,
          (const char **)tmp_prompts.get(),
          tokens_data));
        EA_R_OK(ea_clip_text_enc_inf(
          clip_text_net,
          clip_ctx->clip_text_in,
          clip_ctx->clip_text_out,
          batch_index));
        EA_LOG_NOTICE("clip text encoder VP times: %lu us\n", clip_text_net->inf_vp_time);

        clip_ctx->clip_performance.cvflow_us += clip_text_net->inf_vp_time;
      }
      if ((batch_index < clip_ctx->batch_num) && (unprocessed_prompt_count != 0)) {
        continue;
      }

      for (int k = 0; k < (int)batch_index; k++) {
        tmp_batch_tensor = ea_tensor_new_from_other_sub(clip_ctx->clip_text_out->port, k, k + 1);
        EA_R_ASSERT(tmp_batch_tensor != nullptr);

        for (int m = 0; m < TENSOR_SHAPE_SIZE; m++) {
          prompt_tensor.shape[m] = ea_tensor_shape(tmp_batch_tensor)[m];
        }
        prompt_tensor.p_buffer = (uint8_t *)ea_tensor_data(tmp_batch_tensor);
        prompt_tensor.data_format = EA_P_F32;
        prompt_tensor.pitch = ea_tensor_pitch(tmp_batch_tensor);
        EA_R_OK(ea_normalization_by_norm(&prompt_tensor, &prompt_tensor, EA_P_NORM_L2));

        EA_R_OK(ea_matmul(&base_tensor, &prompt_tensor, &logits_tensor, 100.0f, 0, 1));

        memset(&clip_ctx->confidence_levels.get()[result_index], 0, sizeof(confidence_levels_t));
        text_sorces[result_index] = ((float *)logits_tensor.p_buffer)[0];
        snprintf(
          clip_ctx->confidence_levels.get()[result_index].prompt,
          CLIP_TEXT_MAX_LEN,
          "%s",
          clip_ctx->prompts[result_index].get());
        result_index++;
        if (tmp_batch_tensor != nullptr) {
          ea_tensor_free(tmp_batch_tensor);
          tmp_batch_tensor = nullptr;
        }
      }

      EA_R_BREAK();
      batch_index = 0;
    }
    EA_R_BREAK();

    for (uint32_t i = 0; i < clip_ctx->prompt_count; ++i) {
      clip_ctx->confidence_levels.get()[i].sorce = text_sorces[i];
    }

    EA_R_OK(ea_softmax(text_sorces, clip_ctx->prompt_count, text_sorces));

    clip_ctx->clip_performance.inference_end_time_us = ea_gettime_us();
    clip_ctx->clip_performance.inference_us = clip_ctx->clip_performance.inference_end_time_us -
                                              clip_ctx->clip_performance.inference_start_time_us;

    for (uint32_t i = 0; i < clip_ctx->prompt_count; ++i) {
      clip_ctx->confidence_levels.get()[i].logits = text_sorces[i];
    }

    for (uint32_t i = 0; i < clip_ctx->prompt_count; ++i) {
      for (uint32_t j = 0; j < clip_ctx->prompt_count - i - 1; ++j) {
        if (
          clip_ctx->confidence_levels.get()[j].logits <
          clip_ctx->confidence_levels.get()[j + 1].logits) {
          confidence_levels_t temp = clip_ctx->confidence_levels.get()[j];
          clip_ctx->confidence_levels.get()[j] = clip_ctx->confidence_levels.get()[j + 1];
          clip_ctx->confidence_levels.get()[j + 1] = temp;
        }
      }
    }

    if (clip_ctx->top_k > clip_ctx->prompt_count) {
      EA_LOG_ERROR(
        "The top_k: %d should be less than or equal to the number of texts: %d.\n",
        clip_ctx->top_k,
        clip_ctx->prompt_count);
      rval = -1;
      break;
    }
  } while (0);

  if (rval < 0) {
    if (tmp_batch_tensor != nullptr) {
      ea_tensor_free(tmp_batch_tensor);
      tmp_batch_tensor = nullptr;
    }
  }
  return rval;
}

COOPER_CLIP_API int clip_run_image_similarity_search(clip_context_t * const clip_ctx)
{
  int rval = EA_SUCCESS;
  int image_index = 0, result_index = 0;
  int unprocessed_image_count = 0;
  uint32_t batch_index = 0;
  ea_clip_image_enc_t * clip_image_net = clip_ctx->clip_image;
  ea_tensor_t * tmp_batch_tensor = nullptr;
  ea_postproc_tensor_t base_tensor;
  ea_postproc_tensor_t image_tensor;
  ea_postproc_tensor_t logits_tensor;
  std::unique_ptr<float[]> logits_buffer;
  std::unique_ptr<uint8_t[]> base_buffer;

  do {
    EA_R_ASSERT(clip_ctx != nullptr);

    memset(&logits_tensor, 0, sizeof(logits_tensor));
    for (int i = 0; i < TENSOR_SHAPE_SIZE; ++i) {
      logits_tensor.shape[i] = 1;
    }
    logits_tensor.data_format = EA_P_F32;
    logits_buffer = std::make_unique<float[]>(1);
    EA_R_ASSERT(logits_buffer != nullptr);
    logits_tensor.p_buffer = logits_buffer.get();

    tmp_batch_tensor = ea_tensor_new_from_other_sub(clip_ctx->clip_image_in->port, 0, 1);
    EA_R_ASSERT(tmp_batch_tensor != nullptr);
    if (clip_ctx->paddingcrop) {
      EA_R_OK(clip_preprocess_image_pad(clip_ctx->query_image_path.c_str(), tmp_batch_tensor));
    } else {
      EA_R_OK(clip_preprocess_image_crop(clip_ctx->query_image_path.c_str(), tmp_batch_tensor));
    }
    ea_tensor_free(tmp_batch_tensor);
    tmp_batch_tensor = nullptr;

    clip_ctx->clip_performance.inference_start_time_us = ea_gettime_us();

    EA_R_OK(
      ea_clip_image_enc_inf(clip_image_net, clip_ctx->clip_image_in, clip_ctx->clip_image_out, 1));
    EA_LOG_NOTICE("clip image encoder VP times: %lu us\n", clip_image_net->inf_vp_time);

    clip_ctx->clip_performance.cvflow_us += clip_image_net->inf_vp_time;

    tmp_batch_tensor = ea_tensor_new_from_other_sub(clip_ctx->clip_image_out->port, 0, 1);
    EA_R_ASSERT(tmp_batch_tensor != nullptr);

    for (int i = 0; i < TENSOR_SHAPE_SIZE; ++i) {
      base_tensor.shape[i] = ea_tensor_shape(tmp_batch_tensor)[i];
    }
    base_tensor.data_format = EA_P_F32;
    base_tensor.pitch = ea_tensor_pitch(tmp_batch_tensor);
    size_t tensor_data_size = ea_tensor_size(tmp_batch_tensor);
    base_buffer = std::make_unique<uint8_t[]>(tensor_data_size);
    EA_R_ASSERT(base_buffer != nullptr);
    base_tensor.p_buffer = base_buffer.get();
    memcpy(base_tensor.p_buffer, ea_tensor_data(tmp_batch_tensor), tensor_data_size);
    ea_tensor_free(tmp_batch_tensor);
    tmp_batch_tensor = nullptr;

    EA_R_OK(ea_normalization_by_norm(&base_tensor, &base_tensor, EA_P_NORM_L2));

    unprocessed_image_count = clip_ctx->images_count;
    image_index = 0;
    result_index = 0;
    batch_index = 0;

    while (image_index < (int)clip_ctx->images_count) {
      tmp_batch_tensor =
        ea_tensor_new_from_other_sub(clip_ctx->clip_image_in->port, batch_index, batch_index + 1);
      EA_R_ASSERT(tmp_batch_tensor != nullptr);
      if (clip_ctx->paddingcrop) {
        EA_R_OK(clip_preprocess_image_pad(clip_ctx->images[image_index].get(), tmp_batch_tensor));
      } else {
        EA_R_OK(clip_preprocess_image_crop(clip_ctx->images[image_index].get(), tmp_batch_tensor));
      }
      ea_tensor_free(tmp_batch_tensor);
      tmp_batch_tensor = nullptr;

      ++batch_index;
      --unprocessed_image_count;
      ++image_index;

      if (batch_index == clip_ctx->batch_num || unprocessed_image_count == 0) {
        EA_R_OK(ea_clip_image_enc_inf(
          clip_image_net,
          clip_ctx->clip_image_in,
          clip_ctx->clip_image_out,
          batch_index));
        EA_LOG_NOTICE("clip image encoder VP times: %lu us\n", clip_image_net->inf_vp_time);
        clip_ctx->clip_performance.cvflow_us += clip_image_net->inf_vp_time;

        for (uint32_t k = 0; k < batch_index; ++k) {
          tmp_batch_tensor = ea_tensor_new_from_other_sub(clip_ctx->clip_image_out->port, k, k + 1);
          EA_R_ASSERT(tmp_batch_tensor != nullptr);

          for (int m = 0; m < TENSOR_SHAPE_SIZE; ++m) {
            image_tensor.shape[m] = ea_tensor_shape(tmp_batch_tensor)[m];
          }
          image_tensor.p_buffer = (uint8_t *)ea_tensor_data(tmp_batch_tensor);
          image_tensor.data_format = EA_P_F32;
          image_tensor.pitch = ea_tensor_pitch(tmp_batch_tensor);

          EA_R_OK(ea_normalization_by_norm(&image_tensor, &image_tensor, EA_P_NORM_L2));
          EA_R_OK(ea_matmul(&base_tensor, &image_tensor, &logits_tensor, 100.0f, 0, 1));

          memset(&clip_ctx->confidence_levels.get()[result_index], 0, sizeof(confidence_levels_t));
          clip_ctx->confidence_levels.get()[result_index].sorce =
            ((float *)logits_tensor.p_buffer)[0];
          snprintf(
            clip_ctx->confidence_levels.get()[result_index].image_info,
            CLIP_PATH_MAX_LEN,
            "%s",
            clip_ctx->images[result_index].get());
          ++result_index;

          ea_tensor_free(tmp_batch_tensor);
          tmp_batch_tensor = nullptr;
        }
        batch_index = 0;
      }
    }

    clip_ctx->clip_performance.inference_end_time_us = ea_gettime_us();
    clip_ctx->clip_performance.inference_us = clip_ctx->clip_performance.inference_end_time_us -
                                              clip_ctx->clip_performance.inference_start_time_us;

    for (uint32_t i = 0; i < clip_ctx->images_count - 1; ++i) {
      for (uint32_t j = 0; j < clip_ctx->images_count - i - 1; ++j) {
        if (
          clip_ctx->confidence_levels.get()[j].sorce <
          clip_ctx->confidence_levels.get()[j + 1].sorce) {
          confidence_levels_t temp = clip_ctx->confidence_levels.get()[j];
          clip_ctx->confidence_levels.get()[j] = clip_ctx->confidence_levels.get()[j + 1];
          clip_ctx->confidence_levels.get()[j + 1] = temp;
        }
      }
    }

    if (clip_ctx->top_k > clip_ctx->images_count) {
      EA_LOG_ERROR(
        "The top_k: %d should be less than or equal to the number of images: %d.\n",
        clip_ctx->top_k,
        clip_ctx->images_count);
      rval = -1;
      break;
    }
  } while (0);

  if (rval < 0) {
    if (tmp_batch_tensor != nullptr) {
      ea_tensor_free(tmp_batch_tensor);
      tmp_batch_tensor = nullptr;
    }
  }

  return rval;
}

COOPER_CLIP_API const clip_performance_t * clip_get_performance(
  const clip_context_t * const clip_ctx)
{
  if (clip_ctx == nullptr) {
    EA_LOG_ERROR("clip_ctx is null\n");
    return nullptr;
  }

  return &clip_ctx->clip_performance;
}

COOPER_CLIP_API const confidence_levels_t * clip_get_confidence_levels(
  const clip_context_t * const clip_ctx)
{
  if (clip_ctx == nullptr) {
    EA_LOG_ERROR("clip_ctx is null\n");
    return nullptr;
  }

  return clip_ctx->confidence_levels.get();
}

COOPER_CLIP_API unsigned int clip_get_confidence_levels_count(const clip_context_t * const clip_ctx)
{
  if (clip_ctx == nullptr) {
    EA_LOG_ERROR("clip_ctx is null\n");
    return 0;
  }

  return clip_ctx->top_k;
}
