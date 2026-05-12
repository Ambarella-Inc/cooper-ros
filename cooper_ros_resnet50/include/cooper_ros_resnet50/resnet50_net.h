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

#ifndef RESNET50_NET_H_
#define RESNET50_NET_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ea_tensor_s ea_tensor_t;
typedef struct resnet50_s resnet50_t;

typedef struct resnet50_params_s
{
  int log_level;
  const char * model_path;
  const char * label_path;
  int top_k;
} resnet50_params_t;

resnet50_t * resnet50_new(const resnet50_params_t * params);
void resnet50_free(resnet50_t * resnet50);
ea_tensor_t * resnet50_input(resnet50_t * resnet50);
int resnet50_vp_forward(resnet50_t * resnet50); /*!< accelerated by VP */

typedef struct resnet50_det_s
{
  float score;
  const char * label;
} resnet50_det_t;

typedef struct resnet50_result_s
{
  resnet50_det_t * detections;
  int detection_num;
} resnet50_result_t;

const resnet50_result_t * resnet50_arm_post_process(resnet50_t * resnet50);

typedef struct resnet50_performance_s
{
  unsigned long inference_time_us;
  unsigned long post_process_time_us;
  unsigned long cvflow_time_us;
} resnet50_performance_t;

const resnet50_performance_t * resnet50_performance(resnet50_t * resnet50);

#ifdef __cplusplus
}
#endif

#endif  // RESNET50_NET_H_
