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

#ifndef RETINAFACE_POSTPROC_H_
#define RETINAFACE_POSTPROC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define RETINAFACE_STEP_NUM          3
#define RETINAFACE_ANCHOR_NUM        2
#define RETINAFACE_OUTPUT_TENSOR_NUM 3

static const int s_min_sizes[RETINAFACE_STEP_NUM][RETINAFACE_ANCHOR_NUM] = { {16, 32}, {64, 128}, {256, 512} };
static const int s_steps[RETINAFACE_STEP_NUM] = {8, 16, 32};
static const float s_variance[2] = {0.1, 0.2};

typedef struct retinaface_postproc_tensor_layout_s
{
  size_t shape[4];
  size_t pitch;
} retinaface_postproc_tensor_layout_t;

typedef struct retinaface_postproc_tensor_s
{
  retinaface_postproc_tensor_layout_t layout;
  float * p_data;
} retinaface_postproc_tensor_t;

typedef struct retinaface_postproc_s retinaface_postproc_t;

typedef struct retinaface_postproc_params_s
{
  float conf_threshold; /*!< the threshold for detections */
  float nms_threshold;  /*!< the threshold for nms */
  int max_det_num;      /*!< the maximum number of detections to be returned */
  int normalize;        /*!< the flag to normalize the coordinates */
  retinaface_postproc_tensor_layout_t input_layout;
  retinaface_postproc_tensor_layout_t output_layout[RETINAFACE_OUTPUT_TENSOR_NUM];
} retinaface_postproc_params_t;

typedef struct retinaface_postproc_det_s
{
  float score;
  float x_start; /*!< normalized value */
  float y_start;
  float x_end;
  float y_end;
  float landmarks[5][2];
} retinaface_postproc_det_t;

typedef struct retinaface_postproc_result_s
{
  retinaface_postproc_det_t * detections;
  int detection_num;
} retinaface_postproc_result_t;

retinaface_postproc_t * retinaface_postproc_new(const retinaface_postproc_params_t * params);
void retinaface_postproc_free(retinaface_postproc_t * postproc);

const retinaface_postproc_result_t * retinaface_postproc_run(retinaface_postproc_t * postproc,
  const retinaface_postproc_tensor_t *output_tensors, int output_tensor_num);

#ifdef __cplusplus
}
#endif

#endif  // RETINAFACE_POSTPROC_H_
