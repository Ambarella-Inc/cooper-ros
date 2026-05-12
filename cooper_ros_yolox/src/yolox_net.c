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
#include <float.h>

#include <signal.h>
#include <fcntl.h>

#include <eazyai.h>
#include <eazyai_postprocess.h>

#include "cooper_ros_yolox/yolox_net.h"
#include "ea_postproc_common.h"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

#define YOLOX_MAX_CLASS_NUM       80
#define YOLOX_MAX_LABEL_LEN       128

struct yolox_s
{
  char labels[YOLOX_MAX_CLASS_NUM][YOLOX_MAX_LABEL_LEN + 2];
  int valid_label_count;

  ea_net_t * net;
  ea_tensor_t * input_tensor;
  ea_tensor_t * output_tensor;
  float nms_threshold;
  float conf_threshold;
  int max_det_num;

  void * postproc_ctx;                          // eazyai post-processing context
  ea_postproc_input_t postproc_input;           // Input structure for post-processing
  ea_postproc_detection_bbox_t * bbox_results;  // Output buffer for detection results

  yolox_result_t result;                        // Internal result buffer
  yolox_performance_t performance;
};

// Private function to free YOLOX resources
static void private_free(yolox_t * yolox)
{
  int i;

  if (yolox) {
    if (yolox->result.detections) {
      for (i = 0; i < yolox->max_det_num; i++) {
        if (yolox->result.detections[i].label) {
          free(yolox->result.detections[i].label);
        }
      }
      free(yolox->result.detections);
    }

    if (yolox->postproc_ctx) {
      ea_postp_yolox_deinit(yolox->postproc_ctx);
    }
    if (yolox->bbox_results) {
      free(yolox->bbox_results);
    }
    if (yolox->postproc_input.p_tensor) {
      free(yolox->postproc_input.p_tensor);
    }

    if (yolox->net) {
      ea_net_free(yolox->net);
      yolox->net = NULL;
    }
    free(yolox);
  }
}

yolox_t * yolox_new(const yolox_params_t * params)
{
  int rval = 0;
  yolox_t * yolox = NULL;
  FILE * fp_label = NULL;
  char * endl = NULL;
  int i;
  ea_postproc_detection_config_t postp_cfg;
  ea_postproc_nn_input_info_t nn_input_info;

  do {
    EA_R_ASSERT(params != NULL);
    EA_R_ASSERT(params->model_path != NULL);
    EA_R_ASSERT(params->label_path != NULL);
    EA_LOG_SET_LOCAL(params->log_level);

    yolox = malloc(sizeof(yolox_t));
    memset(yolox, 0, sizeof(yolox_t));
    yolox->nms_threshold = params->nms_threshold;
    yolox->conf_threshold = params->conf_threshold;
    yolox->max_det_num = params->max_det_num;

    yolox->net = ea_net_new(NULL);
    EA_R_ASSERT(yolox->net != NULL);

    EA_R_OK(ea_net_load(yolox->net, EA_NET_LOAD_FILE, (void *)params->model_path, 1 /*max_batch*/));
    yolox->input_tensor = ea_net_input_by_index(yolox->net, 0);
    yolox->output_tensor = ea_net_output_by_index(yolox->net, 0);

    EA_LOG_NOTICE(
      "output shape [%zu, %zu, %zu, %zu]\n",
      ea_tensor_shape(yolox->output_tensor)[EA_N],
      ea_tensor_shape(yolox->output_tensor)[EA_C],
      ea_tensor_shape(yolox->output_tensor)[EA_H],
      ea_tensor_shape(yolox->output_tensor)[EA_W]);

    // Setup post-processing input structure
    yolox->postproc_input.num = 1;
    yolox->postproc_input.p_tensor = malloc(sizeof(ea_postproc_tensor_t));
    EA_R_ASSERT(yolox->postproc_input.p_tensor != NULL);
    memset(yolox->postproc_input.p_tensor, 0, sizeof(ea_postproc_tensor_t));

    // Configure tensor information
    yolox->postproc_input.p_tensor->p_name = "yolox_output";
    yolox->postproc_input.p_tensor->data_format = EA_P_F32;
    yolox->postproc_input.p_tensor->pitch = ea_tensor_pitch(yolox->output_tensor);
    yolox->postproc_input.p_tensor->shape[EA_P_N] = ea_tensor_shape(yolox->output_tensor)[EA_N];
    yolox->postproc_input.p_tensor->shape[EA_P_C] = ea_tensor_shape(yolox->output_tensor)[EA_C];
    yolox->postproc_input.p_tensor->shape[EA_P_H] = ea_tensor_shape(yolox->output_tensor)[EA_H];
    yolox->postproc_input.p_tensor->shape[EA_P_W] = ea_tensor_shape(yolox->output_tensor)[EA_W];

    // Allocate output buffer using dynamic size
    yolox->bbox_results = malloc(sizeof(ea_postproc_detection_bbox_t) * yolox->max_det_num);
    EA_R_ASSERT(yolox->bbox_results != NULL);
    memset(yolox->bbox_results, 0, sizeof(ea_postproc_detection_bbox_t) * yolox->max_det_num);

    // Configure post-processing parameters
    memset(&postp_cfg, 0, sizeof(ea_postproc_detection_config_t));
    postp_cfg.postp_input = yolox->postproc_input;
    postp_cfg.conf_threshold = params->conf_threshold;
    postp_cfg.nms_threshold = params->nms_threshold;
    postp_cfg.output_normalize = 1;            // Enable coordinate normalization

    postp_cfg.class_num = YOLOX_MAX_CLASS_NUM; // COCO dataset class number
    postp_cfg.use_multi_cls = 1;               // YOLOX uses multi-class
    postp_cfg.use_exp = 0;                     // YOLOX does not use exp function
    postp_cfg.box_coord_type = EA_P_BOX_XYXY;  // Use xyxy format
    postp_cfg.topk = 100;                      // Limit topk
    postp_cfg.nms_topk = 100;                  // Limit nms topk
    postp_cfg.background_label_id = 0;         // Background class ID
    postp_cfg.log_level = params->log_level;

    // Set network original input information
    memset(&nn_input_info, 0, sizeof(nn_input_info));
    nn_input_info.p_in_port_name = NULL;
    nn_input_info.shape[EA_P_N] = ea_tensor_shape(yolox->input_tensor)[EA_N];
    nn_input_info.shape[EA_P_C] = ea_tensor_shape(yolox->input_tensor)[EA_C];
    nn_input_info.shape[EA_P_H] = ea_tensor_shape(yolox->input_tensor)[EA_H];
    nn_input_info.shape[EA_P_W] = ea_tensor_shape(yolox->input_tensor)[EA_W];
    postp_cfg.p_nn_orig_input_info = &nn_input_info;
    postp_cfg.nn_orig_input_info_num = 1;

    yolox->postproc_ctx = ea_postp_yolox_init(&postp_cfg, params->class_agnostic);
    EA_R_ASSERT(yolox->postproc_ctx != NULL);

    // load label from file
    fp_label = fopen(params->label_path, "r");
    if (fp_label == NULL) {
      EA_LOG_ERROR("can't open file %s\n", params->label_path);
      rval = -1;
      break;
    }

    yolox->valid_label_count = 0;
    for (i = 0; i < YOLOX_MAX_CLASS_NUM; i++) {
      if (fgets(yolox->labels[i], YOLOX_MAX_LABEL_LEN + 2, fp_label) == NULL) {
        break;
      }
      if (strlen(yolox->labels[i]) > (size_t)YOLOX_MAX_LABEL_LEN) {
        EA_LOG_ERROR("max_label_len %d is too small\n", YOLOX_MAX_LABEL_LEN);
        rval = -1;
        break;
      }
      endl = strchr(yolox->labels[i], '\n');
      if (endl) {
        endl[0] = '\0';
      }
      yolox->valid_label_count++;
    }

    EA_R_BREAK();
    fclose(fp_label);
    fp_label = NULL;
    EA_LOG_NOTICE("label num: %d\n", yolox->valid_label_count);

    // Allocate result memory
    yolox->result.detections = malloc(yolox->max_det_num * sizeof(yolox_det_t));
    EA_R_ASSERT(yolox->result.detections != NULL);
    memset(yolox->result.detections, 0, yolox->max_det_num * sizeof(yolox_det_t));
    yolox->result.valid_det_count = 0;

    // Allocate label memory for each detection
    for (i = 0; i < yolox->max_det_num; i++) {
      yolox->result.detections[i].label = malloc(YOLOX_MAX_LABEL_LEN + 2);
      EA_R_ASSERT(yolox->result.detections[i].label != NULL);
      memset(yolox->result.detections[i].label, 0, YOLOX_MAX_LABEL_LEN + 2);
    }
  } while (0);

  if (rval < 0) {
    if (fp_label) {
      fclose(fp_label);
      fp_label = NULL;
    }

    if (yolox) {
      private_free(yolox);
      yolox = NULL;
    }
  }

  return yolox;
}

void yolox_free(yolox_t * yolox)
{
  private_free(yolox);
  EA_LOG_NOTICE("yolox_free\n");
}

ea_tensor_t * yolox_input(yolox_t * yolox)
{
  return yolox->input_tensor;
}

int yolox_vp_forward(yolox_t * yolox)
{
  int rval = 0;
  unsigned long start_time_us;

  do {
    start_time_us = ea_gettime_us();
    EA_R_OK(ea_net_forward(yolox->net, 1 /*batch*/));
    yolox->performance.inference_time_us = ea_gettime_us() - start_time_us;
    yolox->performance.cvflow_time_us = ea_net_params(yolox->net)->vp_time_us;
  } while (0);

  return rval;
}

static int post_process(yolox_t * yolox)
{
  int rval = 0;
  uint32_t valid_num = 0;
  int i;
  unsigned long start_time_us;

  do {
    EA_R_ASSERT(yolox != NULL);
    EA_R_ASSERT(yolox->postproc_ctx != NULL);

    start_time_us = ea_gettime_us();

    // Update tensor data pointer
    yolox->postproc_input.p_tensor->p_buffer = ea_tensor_data_for_read(yolox->output_tensor, EA_CPU);

    // Execute YOLOX post-processing using eazyai API
    EA_R_OK(ea_postp_yolox(
      yolox->postproc_ctx,
      &yolox->postproc_input,
      yolox->max_det_num,
      yolox->bbox_results,
      &valid_num));

    // Convert eazyai results to yolox_result_t format
    // Don't memset the entire result structure as it will clear the detections pointer
    EA_R_ASSERT((int)valid_num <= yolox->max_det_num);
    yolox->result.valid_det_count = valid_num;

    for (i = 0; i < yolox->result.valid_det_count; i++) {
      yolox->result.detections[i].id = yolox->bbox_results[i].id;
      yolox->result.detections[i].score = yolox->bbox_results[i].score;
      yolox->result.detections[i].x_start = yolox->bbox_results[i].box.x_start;
      yolox->result.detections[i].y_start = yolox->bbox_results[i].box.y_start;
      yolox->result.detections[i].x_end = yolox->bbox_results[i].box.x_end;
      yolox->result.detections[i].y_end = yolox->bbox_results[i].box.y_end;

      // Set label string
      if (yolox->result.detections[i].id < yolox->valid_label_count) {
        strcpy(yolox->result.detections[i].label, yolox->labels[yolox->result.detections[i].id]);
      } else {
        strcpy(yolox->result.detections[i].label, "unknown");
      }

      EA_LOG_DEBUG(
        "Detection %d: class=%d(%s), score=%.3f, bbox=(%.3f,%.3f,%.3f,%.3f)\n",
        i,
        yolox->result.detections[i].id,
        yolox->result.detections[i].label,
        yolox->result.detections[i].score,
        yolox->result.detections[i].x_start,
        yolox->result.detections[i].y_start,
        yolox->result.detections[i].x_end,
        yolox->result.detections[i].y_end);
    }

    yolox->performance.post_process_time_us = ea_gettime_us() - start_time_us;
    EA_LOG_DEBUG(
      "YOLOX (post process eazyai) detected %d objects\n",
      yolox->result.valid_det_count);

  } while (0);

  return rval;
}

const yolox_result_t * yolox_arm_post_process(yolox_t * yolox)
{
  int rval = 0;
  const yolox_result_t * result = NULL;

  do {
    EA_R_ASSERT(yolox != NULL);

    EA_R_OK(post_process(yolox));
    result = &yolox->result;
  } while (0);

  return result;
}

const yolox_performance_t * yolox_performance(yolox_t * yolox)
{
  return &yolox->performance;
}
