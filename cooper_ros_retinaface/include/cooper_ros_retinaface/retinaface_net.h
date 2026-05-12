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

#ifndef RETINAFACE_NET_H_
#define RETINAFACE_NET_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ea_tensor_s ea_tensor_t;
typedef struct retinaface_s retinaface_t;

typedef struct retinaface_params_s
{
  int log_level;
  const char * model_path;
  float conf_threshold; /*!< the threshold for detections */
  float nms_threshold;  /*!< the threshold for nms */
  int max_det_num;      /*!< the maximum number of detections to be returned */
  int sort_by_area;     /*!< the flag to sort the detected faces by area in descending order */
} retinaface_params_t;

retinaface_t * retinaface_new(const retinaface_params_t * params);
void retinaface_free(retinaface_t * retinaface);
ea_tensor_t * retinaface_input(retinaface_t * retinaface);
int retinaface_vp_forward(retinaface_t * retinaface); /*!< accelerated by VP */

typedef struct retinaface_det_s
{
  float score;
  float x_start; /*!< normalized value */
  float y_start;
  float x_end;
  float y_end;
  float landmarks[5][2];
} retinaface_det_t;

typedef struct retinaface_result_s
{
  retinaface_det_t * detections;
  int detection_num;
} retinaface_result_t;

const retinaface_result_t * retinaface_arm_post_process(retinaface_t * retinaface);

typedef struct retinaface_performance_s
{
  unsigned long inference_time_us;
  unsigned long post_process_time_us;
  unsigned long cvflow_time_us;
} retinaface_performance_t;

const retinaface_performance_t * retinaface_performance(retinaface_t * retinaface);

#ifdef __cplusplus
}
#endif

#endif  // RETINAFACE_NET_H_
