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

#ifndef _YOLOX_NET_H_
#define _YOLOX_NET_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ea_tensor_s ea_tensor_t;
typedef struct ea_net_s ea_net_t;

typedef struct yolox_s yolox_t;

typedef struct yolox_params_s
{
  int log_level;

  const char * model_path;
  const char * label_path;

  float conf_threshold; /*!< the threshold for filter_bboxes */
  float nms_threshold;  /*!< the threshold for nms */
  int max_det_num;      /*!< the maximum number of detections */
  bool class_agnostic;   /*!< use multi class or best class in post process. false=multi class, true=best class */
} yolox_params_t;

yolox_t * yolox_new(const yolox_params_t * params);
void yolox_free(yolox_t * yolox);
ea_tensor_t * yolox_input(yolox_t * yolox);
int yolox_vp_forward(yolox_t * yolox);

typedef struct yolox_det_s
{
  float score;
  int id;
  char * label;
  float x_start;  // normalized value
  float y_start;
  float x_end;
  float y_end;
} yolox_det_t;

typedef struct yolox_result_s
{
  yolox_det_t * detections;
  int valid_det_count;
} yolox_result_t;

typedef struct yolox_performance_s
{
  unsigned long inference_time_us;
  unsigned long cvflow_time_us;
  unsigned long post_process_time_us;
} yolox_performance_t;

const yolox_result_t * yolox_arm_post_process(yolox_t * yolox);
const yolox_performance_t * yolox_performance(yolox_t * yolox);

#ifdef __cplusplus
}
#endif

#endif
