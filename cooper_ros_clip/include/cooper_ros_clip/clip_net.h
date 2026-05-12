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

#ifndef CLIP_NET_H_
#define CLIP_NET_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default visibility attribute for exported symbols */
#ifndef COOPER_CLIP_API
#define COOPER_CLIP_API __attribute__((visibility("default")))
#endif

/** @brief Maximum path length for file paths */
#define CLIP_PATH_MAX_LEN 256

/** @brief Maximum text length for queries and content */
#define CLIP_TEXT_MAX_LEN 1024
// =============================================================================
// Enumerations
// =============================================================================

/**
 * @brief CLIP operation modes
 */
typedef enum clip_mode_e
{
  CLIP_MODE_RETRIEVAL = 0,   ///< Text-to-image retrieval mode
  CLIP_MODE_CLASSIFICATION,  ///< Image classification mode
  CLIP_MODE_SEARCH,          ///< Image-to-image search mode
  CLIP_MODE_UNKNOWN          ///< Unknown/uninitialized mode
} clip_mode_t;

// =============================================================================
// Data Structures
// =============================================================================

/**
 * @brief Confidence levels and result information
 */
typedef struct confidence_levels_s
{
  float logits;                        ///< Logits value for retrieval and classification
  float sorce;                         ///< Score value for all modes
  char image_info[CLIP_PATH_MAX_LEN];  ///< Image information for retrieval and search
  char prompt[CLIP_TEXT_MAX_LEN];      ///< Prompt information for classification
} confidence_levels_t;

/**
 * @brief CLIP parameter configuration structure
 */
typedef struct clip_params_s
{
  // Core execution parameters
  clip_mode_t mode;        ///< CLIP execution mode
  unsigned int top_k;      ///< Number of top results to return
  unsigned int batch_num;  ///< Batch size for processing
  int log_level;           ///< Logging level

  // Model paths (required)
  const char * image_model_path;           ///< Path to image encoder model
  const char * text_model_path;            ///< Path to text encoder model
  const char * text_embedded_weight_path;  ///< Path to text embedding weights
  const char * vocab_path;                 ///< Path to vocabulary file

  // Data paths (mode-specific)
  const char * text;             ///< Query text (RETRIEVAL mode)
  const char * images_dir;       ///< Working directory (RETRIEVAL/SEARCH modes)
  const char * image_path;       ///< Single image path (CLASSIFICATION mode)
  const char * prompts_path;     ///< Path to prompts file (CLASSIFICATION mode)
  const char * query_image_path;  ///< Query image path (SEARCH mode)

  // Configuration flags
  unsigned char paddingcrop;                 ///< Padding/crop mode flag
  unsigned char nvp_affinity;                ///< NVP affinity setting
  unsigned char disable_output_fp16_2_fp32;  ///< Disable FP16 to FP32 conversion
} clip_params_t;

/**
 * @brief Timing metrics for performance monitoring
 */
typedef struct clip_performance_s
{
  unsigned long inference_us;             ///< Time from ea_clip_image_enc_inf to ea_softmax
  unsigned long cvflow_us;                ///< Sum of all ea_clip_image and ea_clip_text inf_vp_time
  unsigned long inference_start_time_us;  ///< Inference start time point (microseconds since epoch)
  unsigned long inference_end_time_us;    ///< Inference end time point (microseconds since epoch)
} clip_performance_t;

/**
 * @brief CLIP execution context containing all runtime data
 * @note This is an opaque type - use the provided accessor functions to interact with it
 */
typedef struct clip_context_s clip_context_t;

/**
 * @brief Initialize CLIP context and models
 * @param params Pointer to clip_params_t structure (must not be null)
 * @return Pointer to initialized clip_context_t structure on success, NULL on error
 * @note The caller is responsible for freeing the returned context using clip_context_deinitialize()
 */
COOPER_CLIP_API clip_context_t * clip_context_init(const clip_params_t * const params);

/**
 * @brief Deinitialize CLIP context and free resources
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @note This function frees all resources associated with the context
 */
COOPER_CLIP_API void clip_context_deinit(clip_context_t * const clip_ctx);

/**
 * @brief Run text-to-image retrieval task
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return 0 on success, negative value on error
 * @note The context must be in CLIP_MODE_RETRIEVAL mode
 */
COOPER_CLIP_API int clip_run_text_to_image_retrieval(clip_context_t * const clip_ctx);

/**
 * @brief Run image classification task
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return 0 on success, negative value on error
 * @note The context must be in CLIP_MODE_CLASSIFICATION mode
 */
COOPER_CLIP_API int clip_run_image_classification(clip_context_t * const clip_ctx);

/**
 * @brief Run image-to-image similarity search
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return 0 on success, negative value on error
 * @note The context must be in CLIP_MODE_SEARCH mode
 */
COOPER_CLIP_API int clip_run_image_similarity_search(clip_context_t * const clip_ctx);

/**
 * @brief Get CLIP performance metrics from context
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return Pointer to clip_performance_t structure, or NULL if clip_ctx is null
 * @note The returned pointer is valid as long as the context is valid
 */
COOPER_CLIP_API const clip_performance_t * clip_get_performance(
  const clip_context_t * const clip_ctx);

/**
 * @brief Get confidence levels from CLIP context
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return Pointer to confidence_levels_t array, or NULL if clip_ctx is null or no results
 * @note The returned pointer is valid as long as the context is valid
 */
COOPER_CLIP_API const confidence_levels_t * clip_get_confidence_levels(
  const clip_context_t * const clip_ctx);

/**
 * @brief Get the count of confidence levels from CLIP context
 * @param clip_ctx Pointer to clip_context_t structure (must not be null)
 * @return Number of confidence levels, or 0 if clip_ctx is null
 * @note Returns the number of results based on the current mode
 */
COOPER_CLIP_API unsigned int clip_get_confidence_levels_count(
  const clip_context_t * const clip_ctx);

#ifdef __cplusplus
}
#endif

#endif  // CLIP_NET_H_
