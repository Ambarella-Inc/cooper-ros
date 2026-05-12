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

#include <signal.h>
#include <fcntl.h>

#include <eazyai.h>

#include "cooper_ros_resnet50/resnet50_net.h"

#define RESNET50_MAX_LABEL_NUM 1000
#define RESNET50_MAX_LABEL_LEN 256

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

struct resnet50_s
{
  ea_net_t * net;
  ea_tensor_t * input_tensor;
  ea_tensor_t * output_tensor;
  int top_k;
  char labels[RESNET50_MAX_LABEL_NUM][RESNET50_MAX_LABEL_LEN + 1];
  int label_num;
  resnet50_result_t result;
  resnet50_performance_t performance;
};
typedef struct resnet50_s resnet50_t;

static void private_free(resnet50_t * resnet50)
{
  if (resnet50->net) {
    ea_net_free(resnet50->net);
    resnet50->net = NULL;
  }

  if (resnet50->result.detections) {
    free(resnet50->result.detections);
    resnet50->result.detections = NULL;
  }

  free(resnet50);
}

resnet50_t * resnet50_new(const resnet50_params_t * params)
{
  int rval = 0;
  resnet50_t * resnet50 = NULL;
  FILE * fp_label = NULL;
  ea_net_params_t net_params;
  const ea_data_format_t * data_format = NULL;

  do {
    EA_R_ASSERT(params != NULL);
    EA_R_ASSERT(params->model_path != NULL);
    EA_LOG_SET_LOCAL(params->log_level);
    resnet50 = malloc(sizeof(resnet50_t));
    EA_R_ASSERT(resnet50 != NULL);
    memset(resnet50, 0, sizeof(resnet50_t));
    resnet50->top_k = params->top_k;

    memset(&net_params, 0, sizeof(ea_net_params_t));
    net_params.disable_output_fp32_from_fp16_on_non_fp32_arch = 1;
    if (params->log_level == EA_LOG_LEVEL_VERBOSE) {
      net_params.verbose_print = 1;
    }
    resnet50->net = ea_net_new(&net_params);
    EA_R_ASSERT(resnet50->net != NULL);

    EA_R_OK(
      ea_net_load(resnet50->net, EA_NET_LOAD_FILE, (void *)params->model_path, 1 /*max_batch*/));

    resnet50->input_tensor = ea_net_input_by_index(resnet50->net, 0);
    resnet50->output_tensor = ea_net_output_by_index(resnet50->net, 0);
    data_format = ea_tensor_data_format(resnet50->output_tensor);
    EA_R_ASSERT(
      data_format->sign == 1 && data_format->datasize == 1 && data_format->exp_offset == 0 &&
      data_format->exp_bits == 4);  // only support fp16

    resnet50->result.detections =
      (resnet50_det_t *)malloc(sizeof(resnet50_det_t) * resnet50->top_k);
    EA_R_ASSERT(resnet50->result.detections != NULL);
    memset(resnet50->result.detections, 0, sizeof(resnet50_det_t) * resnet50->top_k);
    resnet50->result.detection_num = 0;

    fp_label = fopen(params->label_path, "r");
    EA_R_ASSERT(fp_label != NULL);
    resnet50->label_num = 0;
    while (fgets(resnet50->labels[resnet50->label_num], RESNET50_MAX_LABEL_LEN + 1, fp_label) !=
           NULL) {
      resnet50->label_num++;
      if (resnet50->label_num >= RESNET50_MAX_LABEL_NUM) {
        break;
      }
    }

    for (int i = 0; i < resnet50->label_num; ++i) {
      char * endl = strchr(resnet50->labels[i], '\n');
      if (i != resnet50->label_num - 1) {  // the last line may have no '\n'
        EA_R_ASSERT(endl != NULL);  // RESNET50_MAX_LABEL_LEN should be enough to hold the label
      }

      if (endl) {
        endl[0] = '\0';  // remove '\n'
      }
    }
    EA_R_BREAK();

    fclose(fp_label);
    fp_label = NULL;

    EA_LOG_NOTICE("resnet50->label_num: %d\n", resnet50->label_num);

    EA_R_ASSERT(resnet50->top_k <= resnet50->label_num);
    EA_R_ASSERT(resnet50->top_k <= (int)ea_tensor_shape(resnet50->output_tensor)[EA_W]);
  } while (0);

  if (rval < 0) {
    if (resnet50) {
      private_free(resnet50);
      resnet50 = NULL;
    }

    if (fp_label) {
      fclose(fp_label);
    }
  }

  return resnet50;
}

void resnet50_free(resnet50_t * resnet50)
{
  if (resnet50) {
    private_free(resnet50);
  }

  EA_LOG_NOTICE("resnet50_free\n");
}

ea_tensor_t * resnet50_input(resnet50_t * resnet50)
{
  return resnet50->input_tensor;
}

int resnet50_vp_forward(resnet50_t * resnet50)
{
  int rval = 0;
  unsigned long start_time_us;

  do {
    start_time_us = ea_gettime_us();
    EA_R_OK(ea_net_forward(resnet50->net, 1 /*batch*/));
    resnet50->performance.inference_time_us = ea_gettime_us() - start_time_us;
    resnet50->performance.cvflow_time_us = ea_net_params(resnet50->net)->vp_time_us;
  } while (0);

  return rval;
}

const resnet50_result_t * resnet50_arm_post_process(resnet50_t * resnet50)
{
  int rval = 0;
  resnet50_result_t * result = &resnet50->result;
  unsigned long start_time_us;
  int output_size = (int)ea_tensor_shape(resnet50->output_tensor)[EA_W];
  __fp16 * output_data = (__fp16 *)ea_tensor_data_for_read(resnet50->output_tensor, EA_CPU);
  int * sort_index = NULL;

  do {
    EA_R_OK(resnet50 != NULL);

    start_time_us = ea_gettime_us();

    sort_index = (int *)malloc(sizeof(int) * output_size);
    EA_R_ASSERT(sort_index != NULL);
    for (int i = 0; i < output_size; i++) {
      sort_index[i] = i;
    }

    for (int i = 0; i < resnet50->top_k && i < output_size - 1; ++i) {
      for (int k = i + 1; k < output_size; ++k) {
        if (output_data[sort_index[i]] < output_data[sort_index[k]]) {
          int temp = sort_index[i];
          sort_index[i] = sort_index[k];
          sort_index[k] = temp;
        }
      }
    }

    for (int i = 0; i < resnet50->top_k; ++i) {
      result->detections[i].score = output_data[sort_index[i]];
      result->detections[i].label = resnet50->labels[sort_index[i]];
    }

    result->detection_num = resnet50->top_k;
    free(sort_index);
    sort_index = NULL;

    resnet50->performance.post_process_time_us = ea_gettime_us() - start_time_us;
  } while (0);

  if (rval < 0) {
    if (sort_index) {
      free(sort_index);
    }

    result = NULL;
  }

  return result;
}

const resnet50_performance_t * resnet50_performance(resnet50_t * resnet50)
{
  return &resnet50->performance;
}
