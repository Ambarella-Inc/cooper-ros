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

#ifndef TOPFORMER_NET_H_
#define TOPFORMER_NET_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ea_tensor_s ea_tensor_t;
typedef struct topformer_s topformer_t;

typedef struct topformer_params_s
{
  int log_level;
  const char * model_path;
} topformer_params_t;

topformer_t * topformer_new(const topformer_params_t * params);
void topformer_free(topformer_t * topformer);
ea_tensor_t * topformer_input(topformer_t * topformer);
ea_tensor_t * topformer_result(topformer_t * topformer);
int topformer_vp_forward(topformer_t * topformer); /*!< accelerated by VP */
int topformer_arm_postprocess(topformer_t * topformer);

typedef struct topformer_performance_s
{
  unsigned long inference_time_us;
  unsigned long post_process_time_us;
  unsigned long cvflow_time_us;
} topformer_performance_t;

const topformer_performance_t * topformer_performance(topformer_t * topformer);

#ifdef __cplusplus
}
#endif

#endif  // TOPFORMER_NET_H_
