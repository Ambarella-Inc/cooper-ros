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

#include <eazyai.h>

#include "cooper_ros_retinaface/retinaface_postproc.h"

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

enum tensor_dimension_e
{
  DIM_N = 0,
  DIM_C = 1,
  DIM_H = 2,
  DIM_W = 3,
};

struct retinaface_postproc_s
{
  float nms_threshold;
  float conf_threshold;
  int max_det_num;
  int normalize;
  int input_h;
  int input_w;
  retinaface_postproc_result_t result;

  int height_group[RETINAFACE_STEP_NUM];
  int width_group[RETINAFACE_STEP_NUM];
  int pred_buffer_group_allocated;
  float *score_pred_group[RETINAFACE_STEP_NUM];
  float *bbox_pred_group[RETINAFACE_STEP_NUM];
  float *landmark_pred_group[RETINAFACE_STEP_NUM];

  int total_box_num;
  float *x1y1x2y2score;
  float *landmarks;
  float *out_x1y1x2y2score;
  float *out_landmarks;
};
typedef struct retinaface_postproc_s retinaface_postproc_t;

static void private_free(retinaface_postproc_t * postproc)
{
  int s;

  if (postproc->pred_buffer_group_allocated) {
    for (s = 0; s < RETINAFACE_STEP_NUM; ++s) {
      if (postproc->score_pred_group[s] != NULL) {
        free(postproc->score_pred_group[s]);
        postproc->score_pred_group[s] = NULL;
      }
      if (postproc->bbox_pred_group[s] != NULL) {
        free(postproc->bbox_pred_group[s]);
        postproc->bbox_pred_group[s] = NULL;
      }
      if (postproc->landmark_pred_group[s] != NULL) {
        free(postproc->landmark_pred_group[s]);
        postproc->landmark_pred_group[s] = NULL;
      }
    }
  }

  if (postproc->x1y1x2y2score != NULL) {
    free(postproc->x1y1x2y2score);
    postproc->x1y1x2y2score = NULL;
  }

  if (postproc->landmarks != NULL) {
    free(postproc->landmarks);
    postproc->landmarks = NULL;
  }

  if (postproc->out_x1y1x2y2score != NULL) {
    free(postproc->out_x1y1x2y2score);
    postproc->out_x1y1x2y2score = NULL;
  }

  if (postproc->out_landmarks != NULL) {
    free(postproc->out_landmarks);
    postproc->out_landmarks = NULL;
  }

  if (postproc->result.detections != NULL) {
    free(postproc->result.detections);
    postproc->result.detections = NULL;
  }

  free(postproc);
}

retinaface_postproc_t * retinaface_postproc_new(const retinaface_postproc_params_t * params)
{
  int rval = 0;
  int s, a;
  retinaface_postproc_t * postproc = NULL;

  do {
    EA_R_ASSERT(params != NULL);
    EA_R_ASSERT(params->max_det_num > 0);
    EA_R_ASSERT(params->conf_threshold > 0.0f);
    EA_R_ASSERT(params->nms_threshold > 0.0f);
    EA_R_ASSERT(params->input_layout.shape[0] > 0);
    EA_R_ASSERT(params->input_layout.shape[1] > 0);
    EA_R_ASSERT(params->input_layout.shape[2] > 0);
    EA_R_ASSERT(params->input_layout.shape[3] > 0);
    EA_R_ASSERT(params->input_layout.pitch > 0);

    postproc = malloc(sizeof(retinaface_postproc_t));
    EA_R_ASSERT(postproc != NULL);
    memset(postproc, 0, sizeof(retinaface_postproc_t));
    postproc->nms_threshold = params->nms_threshold;
    postproc->conf_threshold = params->conf_threshold;
    postproc->max_det_num = params->max_det_num;
    postproc->normalize = params->normalize;
    postproc->input_h = params->input_layout.shape[DIM_H];
    postproc->input_w = params->input_layout.shape[DIM_W];

    for (s = 0; s < RETINAFACE_STEP_NUM; ++s) {
      postproc->height_group[s] = (int)(params->input_layout.shape[DIM_H] / s_steps[s]);
      postproc->width_group[s] = (int)(params->input_layout.shape[DIM_W] / s_steps[s]);
      postproc->total_box_num += postproc->height_group[s] * postproc->width_group[s] * RETINAFACE_ANCHOR_NUM;
    }

    EA_R_ASSERT(postproc->total_box_num > 0);

    // x1, y1, x2, y2, score
    postproc->x1y1x2y2score = (float *)malloc(postproc->total_box_num * 5 * sizeof(float));
    EA_R_ASSERT(postproc->x1y1x2y2score != NULL);
    memset(postproc->x1y1x2y2score, 0, postproc->total_box_num * 5 * sizeof(float));
    // output x1, y1, x2, y2, score
    postproc->out_x1y1x2y2score = (float *)malloc(postproc->total_box_num * sizeof(float) * 5);
    EA_R_ASSERT(postproc->out_x1y1x2y2score != NULL);
    memset(postproc->out_x1y1x2y2score, 0, postproc->total_box_num * sizeof(float) * 5);

    postproc->landmarks = (float *)malloc(postproc->total_box_num * 5 * 2 * sizeof(float));
    EA_R_ASSERT(postproc->landmarks != NULL);
    memset(postproc->landmarks, 0, postproc->total_box_num * 5 * 2 * sizeof(float));
    postproc->out_landmarks = (float *)malloc(postproc->total_box_num * 5 * 2 * sizeof(float));
    EA_R_ASSERT(postproc->out_landmarks != NULL);
    memset(postproc->out_landmarks, 0, postproc->total_box_num * 5 * 2 * sizeof(float));

    postproc->result.detections =
      (retinaface_postproc_det_t *)malloc(sizeof(retinaface_postproc_det_t) * postproc->max_det_num);
    EA_R_ASSERT(postproc->result.detections != NULL);
    memset(postproc->result.detections, 0, sizeof(retinaface_postproc_det_t) * postproc->max_det_num);
    postproc->result.detection_num = 0;

    if (params->output_layout[0].shape[DIM_H] != 1) {
      for (a = 1; a < RETINAFACE_ANCHOR_NUM; ++a) {
        EA_R_ASSERT(params->output_layout[a].shape[DIM_H] != 1);
      }
      EA_R_BREAK();

      for (s = 0; s < RETINAFACE_STEP_NUM; ++s) {
        postproc->score_pred_group[s] = (float *)malloc(postproc->height_group[s] *
          postproc->width_group[s] * RETINAFACE_ANCHOR_NUM * sizeof(float) * 2);
        EA_R_ASSERT(postproc->score_pred_group[s] != NULL);
        postproc->bbox_pred_group[s] = (float *)malloc(postproc->height_group[s] *
          postproc->width_group[s] * RETINAFACE_ANCHOR_NUM * sizeof(float) * 4);
        EA_R_ASSERT(postproc->bbox_pred_group[s] != NULL);
        postproc->landmark_pred_group[s] = (float *)malloc(postproc->height_group[s] *
          postproc->width_group[s] * RETINAFACE_ANCHOR_NUM * sizeof(float) * 10);
        EA_R_ASSERT(postproc->landmark_pred_group[s] != NULL);
      }
      EA_R_BREAK();

      postproc->pred_buffer_group_allocated = 1;
    }
  } while (0);

  if (rval < 0) {
    if (postproc) {
      private_free(postproc);
      postproc = NULL;
    }
  }

  return postproc;
}

void retinaface_postproc_free(retinaface_postproc_t * postproc)
{
  if (postproc) {
    private_free(postproc);
  }
}

static const retinaface_postproc_tensor_t *
get_tensor_by_element_num(const retinaface_postproc_tensor_t *output_tensors, int element_num)
{
  int i;
  const retinaface_postproc_tensor_t * tensor = NULL;

  for (i = 0; i < RETINAFACE_OUTPUT_TENSOR_NUM; ++i) {
    if (output_tensors[i].layout.shape[DIM_H] * output_tensors[i].layout.shape[DIM_W] == (size_t)element_num) {
      tensor = &output_tensors[i];
      break;
    }
  }

  return tensor;
}

static void squeeze_tensor_to_buffer(const retinaface_postproc_tensor_t *tensor,
  const int height[RETINAFACE_STEP_NUM],
  const int width[RETINAFACE_STEP_NUM],
  float * buffers[RETINAFACE_STEP_NUM])
{
  int s;
  int a, h, w;
  uint8_t * src = (uint8_t *)tensor->p_data;
  uint8_t * dst = NULL;
  int copy_size = tensor->layout.shape[DIM_W] * sizeof(float);

  for (s = 0; s < RETINAFACE_STEP_NUM; ++s) {
    dst = (uint8_t *)buffers[s];
    for (h = 0; h < height[s]; ++h) {
      for (w = 0; w < width[s]; ++w) {
        for (a = 0; a < RETINAFACE_ANCHOR_NUM; ++a) {
          memcpy(dst, src, copy_size);
          src += tensor->layout.pitch;
          dst += copy_size;
        }
      }
    }
  }
}

static void slice_tensor_to_buffer(const retinaface_postproc_tensor_t *tensor, int element_size,
  const int height[RETINAFACE_STEP_NUM], const int width[RETINAFACE_STEP_NUM],
  float * buffers[RETINAFACE_STEP_NUM])
{
  int s;

  buffers[0] = (float *)tensor->p_data;
  for (s = 0; s < RETINAFACE_STEP_NUM - 1; ++s) {
    buffers[s + 1] = (float *)tensor->p_data + height[s] * width[s] * element_size;
  }
}

const retinaface_postproc_result_t * retinaface_postproc_run(retinaface_postproc_t * postproc,
  const retinaface_postproc_tensor_t *output_tensors, int output_tensor_num)
{
  int rval = 0;
  const retinaface_postproc_tensor_t * box_tensor = NULL;
  const retinaface_postproc_tensor_t * conf_tensor = NULL;
  const retinaface_postproc_tensor_t * landmark_tensor = NULL;
  int s, a, h, w, n, j;
  int height, width;
  float * score_pred = NULL;
  float * bbox_pred = NULL;
  float * landmark_pred = NULL;
  float anchor_cx, anchor_cy, anchor_s_kx, anchor_s_ky;
  float dx, dy, dw, dh, pred_cx, pred_cy, pred_w, pred_h;
  float * landmarks = NULL;
  float landmark_x, landmark_y;
  float score;
  int valid_det_num = 0;
	int nms_valid_det_num = 0;
  float h_scale, w_scale;
  retinaface_postproc_result_t * result = &postproc->result;

  do {
    EA_R_ASSERT(postproc != NULL);
    EA_R_ASSERT(output_tensors != NULL);
    EA_R_ASSERT(output_tensor_num == RETINAFACE_OUTPUT_TENSOR_NUM);

    box_tensor = get_tensor_by_element_num(output_tensors, postproc->total_box_num * 4);
    conf_tensor = get_tensor_by_element_num(output_tensors, postproc->total_box_num * 2);
    landmark_tensor = get_tensor_by_element_num(output_tensors, postproc->total_box_num * 10);

    EA_R_ASSERT(box_tensor != NULL);
    EA_R_ASSERT(conf_tensor != NULL);
    EA_R_ASSERT(landmark_tensor != NULL);

    if (postproc->pred_buffer_group_allocated) {
      squeeze_tensor_to_buffer(box_tensor, postproc->height_group, postproc->width_group,
        postproc->bbox_pred_group);
      squeeze_tensor_to_buffer(conf_tensor, postproc->height_group, postproc->width_group,
        postproc->score_pred_group);
      squeeze_tensor_to_buffer(landmark_tensor, postproc->height_group, postproc->width_group,
        postproc->landmark_pred_group);
    } else {
      EA_R_ASSERT((int)box_tensor->layout.shape[DIM_W] == postproc->total_box_num * 4);
      EA_R_ASSERT((int)conf_tensor->layout.shape[DIM_W] == postproc->total_box_num * 2);
      EA_R_ASSERT((int)landmark_tensor->layout.shape[DIM_W] == postproc->total_box_num * 10);
      slice_tensor_to_buffer(box_tensor, RETINAFACE_ANCHOR_NUM * 4, postproc->height_group, postproc->width_group,
        postproc->bbox_pred_group);
      slice_tensor_to_buffer(conf_tensor, RETINAFACE_ANCHOR_NUM * 2, postproc->height_group, postproc->width_group,
        postproc->score_pred_group);
      slice_tensor_to_buffer(landmark_tensor, RETINAFACE_ANCHOR_NUM * 10, postproc->height_group, postproc->width_group,
        postproc->landmark_pred_group);
    }

    for (s = 0; s < RETINAFACE_STEP_NUM; ++s) {
      height = postproc->height_group[s];
      width = postproc->width_group[s];
      score_pred = postproc->score_pred_group[s];
      bbox_pred = postproc->bbox_pred_group[s];
      landmark_pred = postproc->landmark_pred_group[s];
      for (h = 0; h < height; ++h) {
        for (w = 0; w < width; ++w) {
          for (a = 0; a < RETINAFACE_ANCHOR_NUM; ++a) {
            score = score_pred[h * width * RETINAFACE_ANCHOR_NUM * 2 + w * RETINAFACE_ANCHOR_NUM * 2 + a * 2 + 1];
            if (score > postproc->conf_threshold) {
              anchor_s_kx = s_min_sizes[s][a];
              anchor_s_ky = s_min_sizes[s][a];
              anchor_cx = (w + 0.5) * s_steps[s];
              anchor_cy = (h + 0.5) * s_steps[s];

              dx = bbox_pred[h * width * RETINAFACE_ANCHOR_NUM * 4 + w * RETINAFACE_ANCHOR_NUM * 4 + a * 4 + 0];
              dy = bbox_pred[h * width * RETINAFACE_ANCHOR_NUM * 4 + w * RETINAFACE_ANCHOR_NUM * 4 + a * 4 + 1];
              dw = bbox_pred[h * width * RETINAFACE_ANCHOR_NUM * 4 + w * RETINAFACE_ANCHOR_NUM * 4 + a * 4 + 2];
              dh = bbox_pred[h * width * RETINAFACE_ANCHOR_NUM * 4 + w * RETINAFACE_ANCHOR_NUM * 4 + a * 4 + 3];
              pred_cx = anchor_cx + dx * s_variance[0] * anchor_s_kx;
              pred_cy = anchor_cy + dy * s_variance[0] * anchor_s_ky;
              pred_w = anchor_s_kx * exp(dw * s_variance[1]);
              pred_h = anchor_s_ky * exp(dh * s_variance[1]);

              postproc->x1y1x2y2score[valid_det_num * 5] =
                EA_MAX(EA_MIN(pred_cx - 0.5 * pred_w, width * s_steps[s] - 1.0), 0.0);
              postproc->x1y1x2y2score[valid_det_num * 5 + 1] =
                EA_MAX(EA_MIN(pred_cy - 0.5 * pred_h, height * s_steps[s] - 1.0), 0.0);
              postproc->x1y1x2y2score[valid_det_num * 5 + 2] =
                EA_MAX(EA_MIN(pred_cx + 0.5 * pred_w, width * s_steps[s] - 1.0), 0.0);
              postproc->x1y1x2y2score[valid_det_num * 5 + 3] =
                EA_MAX(EA_MIN(pred_cy + 0.5 * pred_h, height * s_steps[s] - 1.0), 0.0);
              postproc->x1y1x2y2score[valid_det_num * 5 + 4] = score;

              landmarks = &landmark_pred[h * width * RETINAFACE_ANCHOR_NUM * 10 +
                w * RETINAFACE_ANCHOR_NUM * 10 + a * 10];
              for (n = 0; n < 5; ++n) {
                landmark_x = anchor_cx + landmarks[2 * n] * s_variance[0] * anchor_s_kx;
                landmark_y = anchor_cy + landmarks[2 * n + 1] * s_variance[0] * anchor_s_ky;
                postproc->landmarks[valid_det_num * 10 + 2 * n] =
                  EA_MAX(EA_MIN(landmark_x, width * s_steps[s] - 1.0), 0.0);
                postproc->landmarks[valid_det_num * 10 + 2 * n + 1] =
                  EA_MAX(EA_MIN(landmark_y, height * s_steps[s] - 1.0), 0.0);
              }

              valid_det_num++;
            }
          }
        }
      }
		}

		EA_R_OK(ea_nms(postproc->x1y1x2y2score, postproc->landmarks,
			sizeof(float) * 10, valid_det_num, postproc->nms_threshold, 0/*use_iou_min*/, 0, /*top_k*/
			postproc->out_x1y1x2y2score, postproc->out_landmarks, &nms_valid_det_num));

    if (postproc->normalize == 1) {
      h_scale = 1.0f / postproc->input_h;
      w_scale = 1.0f / postproc->input_w;
    } else {
      h_scale = 1.0f;
      w_scale = 1.0f;
    }

    valid_det_num = EA_MIN(nms_valid_det_num, postproc->max_det_num);
    result->detection_num = valid_det_num;
		for (j = 0; j < valid_det_num; j++) {
      result->detections[j].score = postproc->out_x1y1x2y2score[j * 5 + 4];
      result->detections[j].x_start = postproc->out_x1y1x2y2score[j * 5] * w_scale;
      result->detections[j].y_start = postproc->out_x1y1x2y2score[j * 5 + 1] * h_scale;
      result->detections[j].x_end = postproc->out_x1y1x2y2score[j * 5 + 2] * w_scale;
      result->detections[j].y_end = postproc->out_x1y1x2y2score[j * 5 + 3] * h_scale;
      for (n = 0; n < 5; n++) {
        result->detections[j].landmarks[n][0] = postproc->out_landmarks[j * 10 + 2 * n] * w_scale;
        result->detections[j].landmarks[n][1] = postproc->out_landmarks[j * 10 + 2 * n + 1] * h_scale;
      }
    }
  } while (0);

  if (rval < 0) {
    result = NULL;
  }

  return result;
}
