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

#ifndef LLAVA_NET_H_
#define LLAVA_NET_H_

#ifdef __cplusplus
extern "C" {
#endif

//llava parameters
typedef struct llava_params_s
{
  int llm_mode;
  const char * default_prompt;
  const char * base_path;
  const char * vit_path;
  const char * vit_video_path;
  const char * vit_single_path;
  unsigned int priority;
  unsigned int disable_dsp;
  char * sr_path;
  char * depth_path;
  unsigned int log_level;
  int batch_size;
  unsigned int max_image_frames;
  unsigned int max_video_frames;
  unsigned int max_record_frames;
  unsigned int device;
  unsigned int vit_num;
} llava_params_t;

// Performance structure definition
typedef struct llava_performance_s
{
  float vit_perf;
  float input_perf;
  float output_perf;
  float prefill_tokens;
  float generate_tokens;
} llava_performance_t;

// Stream callback function type definition
typedef void (*llava_stream_callback_t)(const char * token);

typedef struct llava_ctx_s llava_ctx_t;

/**
 * @brief Initialize LLaVA model with given parameters
 * @param llava_params LLaVA parameters structure
 * @return llava_ctx_t* on success, nullptr on error
 */
llava_ctx_t * llavaNetNew(llava_params_t * llava_params);

/**
 * @brief Cleanup and deinitialize LLaVA model
 * @param llava_ctx Application context structure to cleanup
 */
void llavaNetFree(llava_ctx_t * llava_ctx);

/**
 * @brief Process the current image stored in llava_ctx for LLaVA inference
 * @param llava_ctx Application context structure containing the image to process
 * @return 0 on success, negative on error
 */
int processImage(llava_ctx_t * llava_ctx);

/**
 * @brief Run LLaVA inference with the current question stored in llava_ctx
 * @param llava_ctx Application context structure containing the question to process
 * @return 0 on success, negative on error
 */
int runLlavaInference(llava_ctx_t * llava_ctx);

/**
 * @brief Get LLaVA performance metrics
 * @param llava_ctx Application context structure
 * @return llava_performance_t structure containing performance metrics, nullptr if llava_ctx is null
 */
llava_performance_t * getPerformance(llava_ctx_t * llava_ctx);

/**
 * @brief Get pointer to stream callback function
 * @param llava_ctx Application context structure
 * @return llava_stream_callback_t* pointer to stream callback function, NULL if llava_ctx is NULL
 */
llava_stream_callback_t * getStreamCallbackPtr(llava_ctx_t * llava_ctx);

/**
 * @brief Get pointer to current question string
 * @param llava_ctx Application context structure
 * @return void* pointer to current question string, nullptr if llava_ctx is null
 */
void * getCurrentQuestionPtr(llava_ctx_t * llava_ctx);

/**
 * @brief Get pointer to current image
 * @param llava_ctx Application context structure
 * @return void* pointer to current image, nullptr if llava_ctx is null
 */
void * getCurrentImagePtr(llava_ctx_t * llava_ctx);

#ifdef __cplusplus
}
#endif

#endif  // LLAVA_NET_H_
