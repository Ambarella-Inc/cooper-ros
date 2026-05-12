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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <wordexp.h>
#include <float.h>
#include <signal.h>
#include <fcntl.h>

#include <eazyai.h>
#include <eazyai_postprocess.h>

#include "cooper_ros_topformer/topformer_net.h"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

struct topformer_s
{
  ea_net_t * net;
  ea_tensor_t * input_tensor;
  ea_tensor_t * output_tensor;
  ea_tensor_t * result_tensor;

  topformer_performance_t performance;
};
typedef struct topformer_s topformer_t;

static void private_free(topformer_t * topformer)
{
  if (topformer->result_tensor && topformer->result_tensor != topformer->output_tensor) {
    ea_tensor_free(topformer->result_tensor);
    topformer->result_tensor = NULL;
  }

  if (topformer->net) {
    ea_net_free(topformer->net);
    topformer->net = NULL;
  }

  free(topformer);
}

topformer_t * topformer_new(const topformer_params_t * params)
{
  int rval = 0;
  topformer_t * topformer = NULL;
  ea_net_params_t net_param;
  const ea_data_format_t * data_format = NULL;
  wordexp_t exp_path;
  size_t shape[EA_DIM] = {0};

  memset(&exp_path, 0, sizeof(exp_path));
  do {
    EA_R_ASSERT(params != NULL);
    EA_R_ASSERT(params->model_path != NULL);
    EA_LOG_SET_LOCAL(params->log_level);
    topformer = malloc(sizeof(topformer_t));
    EA_R_ASSERT(topformer != NULL);
    memset(topformer, 0, sizeof(topformer_t));

    memset(&net_param, 0, sizeof(net_param));
    net_param.disable_output_fp32_from_fp16_on_non_fp32_arch = 1;
    topformer->net = ea_net_new(&net_param);
    EA_R_ASSERT(topformer->net != NULL);

    if (params->log_level == EA_LOG_LEVEL_VERBOSE) {
      ea_net_params(topformer->net)->verbose_print = 1;
    }

    if (wordexp(params->model_path, &exp_path, 0) != 0) {
      EA_LOG_ERROR("Failed to expand model path: %s", params->model_path);
      break;
    }

    EA_R_OK(
      ea_net_load(topformer->net, EA_NET_LOAD_FILE, (void *)exp_path.we_wordv[0], 1 /*max_batch*/));
    wordfree(&exp_path);
    memset(&exp_path, 0, sizeof(exp_path));

    topformer->input_tensor = ea_net_input_by_index(topformer->net, 0);
    EA_R_ASSERT(topformer->input_tensor != NULL);
    topformer->output_tensor = ea_net_output_by_index(topformer->net, 0);
    EA_R_ASSERT(topformer->output_tensor != NULL);
    data_format = ea_tensor_data_format(topformer->output_tensor);
    EA_R_ASSERT(data_format != NULL);
    if (ea_tensor_shape(topformer->output_tensor)[EA_C] == 1) {
      topformer->result_tensor = topformer->output_tensor;
    } else {
      shape[EA_N] = 1;
      shape[EA_C] = 1;
      shape[EA_H] = ea_tensor_shape(topformer->output_tensor)[EA_H];
      shape[EA_W] = ea_tensor_shape(topformer->output_tensor)[EA_W];
      topformer->result_tensor = ea_tensor_new(EA_U8, shape, shape[EA_W]);
      EA_R_ASSERT(topformer->result_tensor != NULL);
    }
  } while (0);

  if (rval < 0) {
    wordfree(&exp_path);
    if (topformer) {
      private_free(topformer);
      topformer = NULL;
    }
  }

  return topformer;
}

void topformer_free(topformer_t * topformer)
{
  if (topformer) {
    private_free(topformer);
  }
}

ea_tensor_t * topformer_input(topformer_t * topformer)
{
  if (topformer == NULL) {
    return NULL;
  }

  return topformer->input_tensor;
}

ea_tensor_t * topformer_result(topformer_t * topformer)
{
  if (topformer == NULL) {
    return NULL;
  }

  return topformer->result_tensor;
}

int topformer_vp_forward(topformer_t * topformer)
{
  int rval = 0;
  unsigned long start_time_us, end_time_us;

  do {
    EA_R_ASSERT(topformer != NULL);
    EA_R_ASSERT(topformer->net != NULL);

    start_time_us = ea_gettime_us();
    EA_R_OK(ea_net_forward(topformer->net, 1 /*batch*/));
    end_time_us = ea_gettime_us();

    topformer->performance.inference_time_us = end_time_us - start_time_us;
    topformer->performance.cvflow_time_us = ea_net_params(topformer->net)->vp_time_us;
  } while (0);

  return rval;
}

int topformer_arm_postprocess(topformer_t * topformer)
{
  int rval = 0;
  size_t h = 0, w = 0, c = 0;
  int idx = 0;
  uint8_t * result_data = NULL;
  _Float16 * output_data = NULL;
  const size_t * shape = NULL;
  _Float16 max_val = -FLT_MAX;
  int max_idx = 0;
  unsigned long start_time_us;

  start_time_us = ea_gettime_us();

  do {
    if (topformer->result_tensor == topformer->output_tensor) {
      break;
    }

    EA_R_ASSERT(ea_tensor_dtype(topformer->output_tensor) == EA_F16);
    output_data = ea_tensor_data(topformer->output_tensor);
    shape = ea_tensor_shape(topformer->output_tensor);

    result_data = ea_tensor_data(topformer->result_tensor);
    memset(result_data, 0, ea_tensor_size(topformer->result_tensor));

    //argmax(1)
    for (h = 0; h < shape[EA_H]; h++) {
      for (w = 0; w < shape[EA_W]; w++) {
        max_val = -FLT_MAX;
        max_idx = 0;

        for (c = 0; c < shape[EA_C]; c++) {
          idx = c * shape[EA_H] * shape[EA_W] + h * shape[EA_W] + w;
          if (output_data[idx] > max_val) {
            max_val = output_data[idx];
            max_idx = c;
          }
        }
        result_data[h * shape[EA_W] + w] = max_idx;
      }
    }
  } while (0);

  topformer->performance.post_process_time_us = ea_gettime_us() - start_time_us;

  return rval;
}

const topformer_performance_t * topformer_performance(topformer_t * topformer)
{
  if (topformer == NULL) {
    return NULL;
  }

  return &topformer->performance;
}
