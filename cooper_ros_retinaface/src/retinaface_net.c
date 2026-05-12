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

#include "cooper_ros_retinaface/retinaface_postproc.h"
#include "cooper_ros_retinaface/retinaface_net.h"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

struct retinaface_s
{
  int sort_by_area;

  ea_net_t * net;
  ea_tensor_t * input_tensor;
  ea_tensor_t * output_tensors[RETINAFACE_OUTPUT_TENSOR_NUM];

  retinaface_postproc_t * postproc;

  retinaface_result_t result;
  retinaface_performance_t performance;
};
typedef struct retinaface_s retinaface_t;

static void private_free(retinaface_t * retinaface)
{
  if (retinaface->postproc != NULL) {
    retinaface_postproc_free(retinaface->postproc);
    retinaface->postproc = NULL;
  }

  if (retinaface->result.detections != NULL) {
    free(retinaface->result.detections);
    retinaface->result.detections = NULL;
  }

  if (retinaface->net) {
    ea_net_free(retinaface->net);
    retinaface->net = NULL;
  }

  free(retinaface);
}

retinaface_t * retinaface_new(const retinaface_params_t * params)
{
  int rval = 0;
  retinaface_t * retinaface = NULL;
  int i;
  retinaface_postproc_params_t postproc_params;
  const ea_data_format_t * data_format = NULL;

  do {
    EA_R_ASSERT(params != NULL);
    EA_R_ASSERT(params->model_path != NULL);
    EA_LOG_SET_LOCAL(params->log_level);
    retinaface = malloc(sizeof(retinaface_t));
    EA_R_ASSERT(retinaface != NULL);
    memset(retinaface, 0, sizeof(retinaface_t));
    retinaface->sort_by_area = params->sort_by_area;

    retinaface->net = ea_net_new(NULL);
    EA_R_ASSERT(retinaface->net != NULL);

    if (params->log_level == EA_LOG_LEVEL_VERBOSE) {
      ea_net_params(retinaface->net)->verbose_print = 1;
    }

    EA_R_OK(
      ea_net_load(retinaface->net, EA_NET_LOAD_FILE, (void *)params->model_path, 1 /*max_batch*/));

    retinaface->input_tensor = ea_net_input_by_index(retinaface->net, 0);

    EA_R_ASSERT(ea_net_output_num(retinaface->net) == RETINAFACE_OUTPUT_TENSOR_NUM);

    for (i = 0; i < RETINAFACE_OUTPUT_TENSOR_NUM; ++i) {
      retinaface->output_tensors[i] = ea_net_output_by_index(retinaface->net, i);
      EA_R_ASSERT(retinaface->output_tensors[i] != NULL);
      data_format = ea_tensor_data_format(retinaface->output_tensors[i]);
      EA_R_ASSERT(
        data_format->sign == 1 && data_format->datasize == 2 && data_format->exp_offset == 0 &&
        data_format->exp_bits == 7);  // fp16 should be converted to fp32 by ea_net_*
    }

    postproc_params.conf_threshold = params->conf_threshold;
    postproc_params.nms_threshold = params->nms_threshold;
    postproc_params.max_det_num = params->max_det_num;
    postproc_params.normalize = 1;
    postproc_params.input_layout.shape[0] = ea_tensor_shape(retinaface->input_tensor)[0];
    postproc_params.input_layout.shape[1] = ea_tensor_shape(retinaface->input_tensor)[1];
    postproc_params.input_layout.shape[2] = ea_tensor_shape(retinaface->input_tensor)[2];
    postproc_params.input_layout.shape[3] = ea_tensor_shape(retinaface->input_tensor)[3];
    postproc_params.input_layout.pitch = ea_tensor_pitch(retinaface->input_tensor);
    for (i = 0; i < RETINAFACE_OUTPUT_TENSOR_NUM; ++i) {
      postproc_params.output_layout[i].shape[0] = ea_tensor_shape(retinaface->output_tensors[i])[0];
      postproc_params.output_layout[i].shape[1] = ea_tensor_shape(retinaface->output_tensors[i])[1];
      postproc_params.output_layout[i].shape[2] = ea_tensor_shape(retinaface->output_tensors[i])[2];
      postproc_params.output_layout[i].shape[3] = ea_tensor_shape(retinaface->output_tensors[i])[3];
      postproc_params.output_layout[i].pitch = ea_tensor_pitch(retinaface->output_tensors[i]);
    }
    retinaface->postproc = retinaface_postproc_new(&postproc_params);
    EA_R_ASSERT(retinaface->postproc != NULL);

    retinaface->result.detections = (retinaface_det_t *)malloc(sizeof(retinaface_det_t) * params->max_det_num);
    EA_R_ASSERT(retinaface->result.detections != NULL);
    memset(retinaface->result.detections, 0, sizeof(retinaface_det_t) * params->max_det_num);
    retinaface->result.detection_num = 0;
  } while (0);

  if (rval < 0) {
    if (retinaface) {
      private_free(retinaface);
      retinaface = NULL;
    }
  }

  return retinaface;
}

void retinaface_free(retinaface_t * retinaface)
{
  if (retinaface) {
    private_free(retinaface);
  }

  EA_LOG_NOTICE("retinaface_free\n");
}

ea_tensor_t * retinaface_input(retinaface_t * retinaface)
{
  return retinaface->input_tensor;
}

int retinaface_vp_forward(retinaface_t * retinaface)
{
  int rval = 0;
  unsigned long start_time_us;

  do {
    start_time_us = ea_gettime_us();
    EA_R_OK(ea_net_forward(retinaface->net, 1 /*batch*/));
    retinaface->performance.inference_time_us = ea_gettime_us() - start_time_us;
    retinaface->performance.cvflow_time_us = ea_net_params(retinaface->net)->vp_time_us;
  } while (0);

  return rval;
}

const retinaface_result_t * post_process(retinaface_t * retinaface)
{
  int rval = 0;
  int i, k;
  retinaface_det_t detection;
  float area1, area2;
  unsigned long start_time_us;
  const retinaface_postproc_result_t * postproc_result = NULL;
  retinaface_result_t * result = &retinaface->result;
  retinaface_postproc_tensor_t output_tensors[RETINAFACE_OUTPUT_TENSOR_NUM];

  do {
    EA_R_OK(retinaface != NULL);

    start_time_us = ea_gettime_us();

    for (i = 0; i < RETINAFACE_OUTPUT_TENSOR_NUM; i++) {
      output_tensors[i].layout.shape[0] = ea_tensor_shape(retinaface->output_tensors[i])[0];
      output_tensors[i].layout.shape[1] = ea_tensor_shape(retinaface->output_tensors[i])[1];
      output_tensors[i].layout.shape[2] = ea_tensor_shape(retinaface->output_tensors[i])[2];
      output_tensors[i].layout.shape[3] = ea_tensor_shape(retinaface->output_tensors[i])[3];
      output_tensors[i].layout.pitch = ea_tensor_pitch(retinaface->output_tensors[i]);
      output_tensors[i].p_data =
        (float *)ea_tensor_data_for_read(retinaface->output_tensors[i], EA_CPU);
    }

    postproc_result = retinaface_postproc_run(retinaface->postproc, output_tensors, RETINAFACE_OUTPUT_TENSOR_NUM);
    EA_R_ASSERT(postproc_result != NULL);

    result->detection_num = postproc_result->detection_num;
    for (i = 0; i < result->detection_num; ++i) {
      result->detections[i].score = postproc_result->detections[i].score;
      result->detections[i].x_start = postproc_result->detections[i].x_start;
      result->detections[i].y_start = postproc_result->detections[i].y_start;
      result->detections[i].x_end = postproc_result->detections[i].x_end;
      result->detections[i].y_end = postproc_result->detections[i].y_end;
      for (k = 0; k < 5; k++) {
        result->detections[i].landmarks[k][0] = postproc_result->detections[i].landmarks[k][0];
        result->detections[i].landmarks[k][1] = postproc_result->detections[i].landmarks[k][1];
      }
    }

    if (retinaface->sort_by_area) {
      for (i = 0; i < result->detection_num - 1; ++i) {
        area1 = (result->detections[i].x_end - result->detections[i].x_start) *
                (result->detections[i].y_end - result->detections[i].y_start);
        for (k = i + 1; k < result->detection_num; ++k) {
          area2 = (result->detections[k].x_end - result->detections[k].x_start) *
                  (result->detections[k].y_end - result->detections[k].y_start);
          if (area1 < area2) {
            memcpy(&detection, &result->detections[i], sizeof(retinaface_det_t));
            memcpy(&result->detections[i], &result->detections[k], sizeof(retinaface_det_t));
            memcpy(&result->detections[k], &detection, sizeof(retinaface_det_t));
            area1 = (result->detections[i].x_end - result->detections[i].x_start) *
                    (result->detections[i].y_end - result->detections[i].y_start);
          }
        }
      }
    }

    retinaface->performance.post_process_time_us = ea_gettime_us() - start_time_us;
  } while (0);

  if (rval < 0) {
    result = NULL;
  }

  return result;
}

const retinaface_result_t * retinaface_arm_post_process(retinaface_t * retinaface)
{
  int rval = 0;
  const retinaface_result_t * result = NULL;

  do {
    EA_R_OK(retinaface != NULL);
    result = post_process(retinaface);
  } while (0);

  return result;
}

const retinaface_performance_t * retinaface_performance(retinaface_t * retinaface)
{
  return &retinaface->performance;
}
