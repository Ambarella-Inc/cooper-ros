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

#include "cooper_ros_llava/llava_net.h"
#include "eazyai.h"
#include "eazyai_io.h"
#include "shepherd.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <iostream>
#include <string>

#define POS_MARGIN         (768)
#define MAX_USER_BUFF_SIZE (512 * 1024)

// Application context structure
typedef struct llava_ctx_s
{
  ea_net_t * sr_net;
  uint8_t sr_enable;
  uint8_t depth_enable;
  ea_net_t * depth_net;
  ea_tensor_t * depth_rgb;
  ea_tensor_t * vit_img;
  ea_tensor_t ** vit_imgs;
  ea_tensor_t * single_vit_img;
  ea_tensor_t * video;
  ea_tensor_t ** video_frames;
  char ** filenames;
  char ** filepaths;
  ea_device_t device;
  uint8_t shepherd_inited;

  struct shepd_config llm_cfg;
  void * model_handle;
  uint8_t is_inited;
  int8_t vit_type;

  unsigned long vit_perf;
  void * usr_ctx;
  bool initialized;

  llava_params_t * params;
  llava_performance_t performance;

  cv::Mat current_image;
  std::string current_question;

  // Stream callback for LLaVA inference
  llava_stream_callback_t stream_callback;
} llava_ctx_t;

enum vit_mode
{
  SINGLE,
  MULTI,
  VIDEO,
};

enum llm_mode
{
  LLAVA = 0,
  LLAVA_OV = 1,
};

struct perf_ctx
{
  struct timeval tv;
  uint32_t pos;
};

EA_LOG_DECLARE_LOCAL(EA_LOG_LEVEL_NOTICE);

static void perfRecord(struct perf_ctx * ctx, uint32_t pos)
{
  gettimeofday(&ctx->tv, NULL);
  ctx->pos = pos;
}

static float perfTimeReport(struct perf_ctx * start, struct perf_ctx * end)
{
  float diff_t =
    end->tv.tv_sec - start->tv.tv_sec + (float)(end->tv.tv_usec - start->tv.tv_usec) / 1000000;

  return diff_t;
}

static float perfSpeedReport(struct perf_ctx * start, struct perf_ctx * end)
{
  float diff_t =
    end->tv.tv_sec - start->tv.tv_sec + (float)(end->tv.tv_usec - start->tv.tv_usec) / 1000000;

  return (end->pos - start->pos) / diff_t;
}

static void deinitDemo(llava_ctx_t * llava_ctx)
{
  uint32_t i = 0;
  uint32_t max_frame_num = 0;
  llava_params_t * params = llava_ctx->params;

  if (params->llm_mode == LLAVA_OV) {
    max_frame_num = params->max_image_frames > params->max_video_frames ? params->max_image_frames
                                                                        : params->max_video_frames;
    max_frame_num = max_frame_num > 1 ? max_frame_num : 1;
  } else {
    max_frame_num = 1;
  }

  if (llava_ctx->vit_imgs != NULL) {
    for (i = 0; i < ea_tensor_shape(llava_ctx->vit_img)[EA_N]; ++i) {
      ea_tensor_free(llava_ctx->vit_imgs[i]);
      llava_ctx->vit_imgs[i] = NULL;
    }
    free(llava_ctx->vit_imgs);
    llava_ctx->vit_imgs = NULL;
  }
  if (llava_ctx->vit_img != NULL) {
    ea_tensor_free(llava_ctx->vit_img);
    llava_ctx->vit_img = NULL;
  }
  if (llava_ctx->single_vit_img != NULL) {
    ea_tensor_free(llava_ctx->single_vit_img);
    llava_ctx->single_vit_img = NULL;
  }
  if (llava_ctx->video_frames != NULL) {
    for (i = 0; i < ea_tensor_shape(llava_ctx->video)[EA_N]; ++i) {
      ea_tensor_free(llava_ctx->video_frames[i]);
      llava_ctx->video_frames[i] = NULL;
    }
    free(llava_ctx->video_frames);
    llava_ctx->video_frames = NULL;
  }
  if (llava_ctx->video != NULL) {
    ea_tensor_free(llava_ctx->video);
    llava_ctx->video = NULL;
  }

  if (llava_ctx->filenames != NULL) {
    for (i = 0; i < max_frame_num; ++i) {
      free(llava_ctx->filenames[i]);
      llava_ctx->filenames[i] = NULL;
    }
    free(llava_ctx->filenames);
    llava_ctx->filenames = NULL;
  }
  if (llava_ctx->filepaths != NULL) {
    for (i = 0; i < max_frame_num; ++i) {
      free(llava_ctx->filepaths[i]);
      llava_ctx->filepaths[i] = NULL;
    }
    free(llava_ctx->filepaths);
    llava_ctx->filepaths = NULL;
  }
  if (llava_ctx->sr_net != NULL) {
    ea_net_free(llava_ctx->sr_net);
    llava_ctx->sr_net = NULL;
  }
  if (llava_ctx->depth_net != NULL) {
    ea_net_free(llava_ctx->depth_net);
    llava_ctx->depth_net = NULL;
  }
  if (llava_ctx->depth_rgb != NULL) {
    ea_tensor_free(llava_ctx->depth_rgb);
    llava_ctx->depth_rgb = NULL;
  }
  // Free llava_ex->vit allocated by calloc
  if (params->llm_mode == LLAVA_OV) {
    struct llava_onevision_extra * llava_ex = &llava_ctx->llm_cfg.shepd_ex.llava_onevision_ex;
    if (llava_ex->vit != NULL) {
      free(llava_ex->vit);
      llava_ex->vit = NULL;
    }
  }

  if (llava_ctx->model_handle != NULL) {
    if (shepherd_model_release(llava_ctx->model_handle) < 0) {
      EA_LOG_ERROR("llava model release err.\n");
    }
    llava_ctx->model_handle = NULL;
  }

  if (llava_ctx->shepherd_inited) {  // otherwise, shepherd_exit gives segmentation fault.
    shepherd_exit();
  }
  printf("\033[0;0;0m\033[0m");
  printf("See you next time.\n");
}

static int initLlavaOv(llava_ctx_t * llava_ctx, uint32_t max_frame_num)
{
  int rval = EA_SUCCESS;
  uint32_t i = 0;
  struct llava_onevision_extra * llava_ex;
  size_t vit_img_shape[EA_DIM];
  size_t video_frame_shape[EA_DIM];
  llava_params_t * params = llava_ctx->params;

  do {
    memset(&vit_img_shape, 0, sizeof(vit_img_shape));
    memset(&video_frame_shape, 0, sizeof(video_frame_shape));

    llava_ctx->vit_type = MULTI;
    llava_ex = &llava_ctx->llm_cfg.shepd_ex.llava_onevision_ex;
    EA_R_ASSERT(params->vit_num > 0);
    params->vit_num = params->vit_num > 2 ? 2 : params->vit_num;
    llava_ex->size = params->vit_num;
    printf("Initializing network mode number: %d\n", llava_ex->size);
    llava_ex->vit =
      (struct llava_onevision_vit *)calloc(llava_ex->size, sizeof(struct llava_onevision_vit));
    EA_R_ASSERT(llava_ex->vit != NULL);
    EA_R_ASSERT(params->vit_path != NULL);
    llava_ex->vit[0].vit_net_fn = (char *)params->vit_path;
    llava_ex->vit[0].max_img_num = max_frame_num;
    llava_ex->vit[0].vit_mode = MULTI;
    if (params->vit_video_path) {
      llava_ex->vit[1].vit_net_fn = (char *)params->vit_video_path;
      llava_ex->vit[1].max_img_num = max_frame_num;
      llava_ex->vit[1].vit_mode = VIDEO;
    } else if (params->vit_single_path) {
      llava_ex->vit[1].vit_net_fn = (char *)params->vit_single_path;
      llava_ex->vit[1].max_img_num = 1;
      llava_ex->vit[1].vit_mode = SINGLE;
    } else {
      // pass
    }
    llava_ctx->model_handle = shepherd_model_create(&llava_ctx->llm_cfg);
    if (llava_ctx->model_handle == NULL) {
      EA_LOG_ERROR("shepherd_model_create failed. Create model error.\n");
      rval = EA_FAIL;
      break;
    }
    for (i = 0; i < params->vit_num; ++i) {
      EA_R_ASSERT(llava_ex->vit[i].vit_in.data_fmt.sign == 0);
      EA_R_ASSERT(llava_ex->vit[i].vit_in.data_fmt.size == 0);
      EA_R_ASSERT(llava_ex->vit[i].vit_in.data_fmt.expoffset == 0);
      EA_R_ASSERT(llava_ex->vit[i].vit_in.data_fmt.expbits == 0);
    }

    vit_img_shape[EA_N] = llava_ex->vit[0].vit_in.dim.plane * max_frame_num;
    vit_img_shape[EA_C] = llava_ex->vit[0].vit_in.dim.depth;
    vit_img_shape[EA_H] = llava_ex->vit[0].vit_in.dim.height;
    vit_img_shape[EA_W] = llava_ex->vit[0].vit_in.dim.width;
    llava_ctx->vit_img = ea_tensor_new(EA_U8, vit_img_shape, 0);
    EA_R_ASSERT(llava_ctx->vit_img != NULL);
    llava_ctx->vit_imgs = (ea_tensor_t **)calloc(max_frame_num, sizeof(unsigned long));
    EA_R_ASSERT(llava_ctx->vit_imgs != NULL);
    printf("max_frame_num %d\n", max_frame_num);
    for (i = 0; i < max_frame_num; ++i) {
      llava_ctx->vit_imgs[i] = ea_tensor_new_from_other_sub(llava_ctx->vit_img, i, i + 1);
      EA_R_ASSERT(llava_ctx->vit_imgs[i] != NULL);
    }
    llava_ex->vit[0].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->vit_img);
    llava_ex->vit[0].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->vit_img);

    if (params->vit_single_path) {
      vit_img_shape[EA_N] = llava_ex->vit[1].vit_in.dim.plane;
      vit_img_shape[EA_C] = llava_ex->vit[1].vit_in.dim.depth;
      vit_img_shape[EA_H] = llava_ex->vit[1].vit_in.dim.height;
      vit_img_shape[EA_W] = llava_ex->vit[1].vit_in.dim.width;
      llava_ctx->single_vit_img = ea_tensor_new(EA_U8, vit_img_shape, 0);
      EA_R_ASSERT(llava_ctx->single_vit_img != NULL);
      llava_ex->vit[1].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->single_vit_img);
      llava_ex->vit[1].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->single_vit_img);
    } else if (params->vit_video_path) {
      video_frame_shape[EA_N] = llava_ex->vit[1].vit_in.dim.plane * params->max_record_frames;
      video_frame_shape[EA_C] = llava_ex->vit[1].vit_in.dim.depth;
      video_frame_shape[EA_H] = llava_ex->vit[1].vit_in.dim.height;
      video_frame_shape[EA_W] = llava_ex->vit[1].vit_in.dim.width;
      llava_ctx->video = ea_tensor_new(EA_U8, video_frame_shape, 0);
      EA_R_ASSERT(llava_ctx->video != NULL);
      llava_ctx->video_frames =
        (ea_tensor_t **)calloc(params->max_record_frames, sizeof(unsigned long));
      EA_R_ASSERT(llava_ctx->video_frames != NULL);
      for (i = 0; i < params->max_record_frames; ++i) {
        llava_ctx->video_frames[i] = ea_tensor_new_from_other_sub(llava_ctx->video, i, i + 1);
        EA_R_ASSERT(llava_ctx->video_frames[i] != NULL);
      }
      llava_ex->vit[1].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->video);
      llava_ex->vit[1].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->video);
    } else {
      // pass
    }
  } while (0);

  return rval;
}

static int initLlava(llava_ctx_t * llava_ctx)
{
  int rval = EA_SUCCESS;
  struct llava_extra * llava_ex;
  size_t vit_img_shape[EA_DIM];
  llava_params_t * params = llava_ctx->params;

  do {
    llava_ex = &llava_ctx->llm_cfg.shepd_ex.llava_ex;
    llava_ctx->vit_type = SINGLE;
    llava_ctx->llm_cfg.shepd_ex.llava_ex.vit_net_fn = (char *)params->vit_path;
    llava_ctx->llm_cfg.shepd_ex.llava_ex.img_start_token_id = 29871;
    llava_ctx->llm_cfg.shepd_ex.llava_ex.img_end_token_id = 29871;
    llava_ctx->llm_cfg.shepd_ex.llava_ex.vit_in.internal_cavalry_mem = 0;
    llava_ctx->model_handle = shepherd_model_create(&llava_ctx->llm_cfg);
    if (llava_ctx->model_handle == NULL) {
      EA_LOG_ERROR("shepherd_model_create failed. Create model error.\n");
      rval = EA_FAIL;
      break;
    }
    EA_R_ASSERT(llava_ex->vit_in.data_fmt.sign == 0);
    EA_R_ASSERT(llava_ex->vit_in.data_fmt.size == 0);
    EA_R_ASSERT(llava_ex->vit_in.data_fmt.expoffset == 0);
    EA_R_ASSERT(llava_ex->vit_in.data_fmt.expbits == 0);

    vit_img_shape[EA_N] = llava_ex->vit_in.dim.plane;
    vit_img_shape[EA_C] = llava_ex->vit_in.dim.depth;
    vit_img_shape[EA_H] = llava_ex->vit_in.dim.height;
    vit_img_shape[EA_W] = llava_ex->vit_in.dim.width;
    llava_ctx->single_vit_img = ea_tensor_new(EA_U8, vit_img_shape, 0);
    EA_R_ASSERT(llava_ctx->single_vit_img != NULL);
    llava_ex->vit_in.img_mem.virt = ea_tensor_data(llava_ctx->single_vit_img);
    llava_ex->vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->single_vit_img);

  } while (0);

  return rval;
}

static int initDemo(llava_ctx_t * llava_ctx, llava_params_t * params)
{
  int rval = EA_SUCCESS;
  uint32_t i = 0;
  struct shepherd_version ver;
  struct shepd_init_cfg init_cfg;
  size_t depth_rgb_shape[EA_DIM];
  ea_net_params_t net_params;
  uint32_t max_frame_num = 0;

  do {
    memset(&ver, 0, sizeof(ver));
    memset(&init_cfg, 0, sizeof(init_cfg));

    // Init LLM
    if (shepherd_get_version(&ver) < 0) {
      EA_LOG_ERROR("shepherd_get_version err.\n");
      rval = EA_FAIL;
      break;
    }
    printf(
      "%s => ver: %d.%d.%d, mod_time: 0x%x.\n",
      ver.description,
      ver.major,
      ver.minor,
      ver.patch,
      ver.mod_time);
    init_cfg.log_level = params->log_level;
    shepherd_init(&init_cfg);
    llava_ctx->shepherd_inited = 1;

    llava_ctx->llm_cfg.batch_size = params->batch_size;
    llava_ctx->llm_cfg.max_user_num = 1;
    llava_ctx->llm_cfg.model_path = params->base_path;
    llava_ctx->llm_cfg.device.device_type = SHEPD_DEVICE_LOCAL;

    if (params->llm_mode == LLAVA_OV) {
      max_frame_num = params->max_image_frames > params->max_video_frames
                        ? params->max_image_frames
                        : params->max_video_frames;
      max_frame_num = max_frame_num > 1 ? max_frame_num : 1;
      if (max_frame_num > params->max_record_frames) {
        EA_LOG_ERROR("Required frame number [%u] too large.\n", max_frame_num);
      }
      EA_R_OK(initLlavaOv(llava_ctx, max_frame_num));
    } else if (params->llm_mode == LLAVA) {
      max_frame_num = 1;
      EA_R_OK(initLlava(llava_ctx));
    } else {
      EA_LOG_ERROR("Not supported LLM mode [%d].\n", params->llm_mode);
      rval = EA_FAIL;
      break;
    }

    llava_ctx->filenames = (char **)calloc(max_frame_num, sizeof(unsigned long));
    EA_R_ASSERT(llava_ctx->filenames != NULL);
    llava_ctx->filepaths = (char **)calloc(max_frame_num, sizeof(unsigned long));
    EA_R_ASSERT(llava_ctx->filepaths != NULL);
    for (i = 0; i < max_frame_num; ++i) {
      llava_ctx->filenames[i] = (char *)calloc(EA_IO_MAX_FILENAME_LENGTH, sizeof(char));
      llava_ctx->filepaths[i] = (char *)calloc(EA_IO_MAX_PATH_LENGTH, sizeof(char));
    }
    if (params->sr_path) {
      memset(&net_params, 0, sizeof(net_params));
      net_params.disable_output_fp32_from_fp16_on_non_fp32_arch = 1;
      net_params.priority = params->priority;
      llava_ctx->sr_net = ea_net_new(&net_params);
      EA_R_ASSERT(llava_ctx->sr_net != NULL);
      EA_R_OK(ea_net_load(llava_ctx->sr_net, EA_NET_LOAD_FILE, (void *)params->sr_path, 1));
    }
    if (params->depth_path) {
      memset(&net_params, 0, sizeof(net_params));
      net_params.disable_output_fp32_from_fp16_on_non_fp32_arch = 0;
      net_params.priority = params->priority;
      llava_ctx->depth_net = ea_net_new(&net_params);
      EA_R_ASSERT(llava_ctx->depth_net != NULL);
      EA_R_OK(ea_net_load(llava_ctx->depth_net, EA_NET_LOAD_FILE, (void *)params->depth_path, 1));
      depth_rgb_shape[EA_N] = 1;
      depth_rgb_shape[EA_C] = 3;
      depth_rgb_shape[EA_H] =
        ea_tensor_shape(ea_net_output_by_index(llava_ctx->depth_net, 0))[EA_H];
      depth_rgb_shape[EA_W] =
        ea_tensor_shape(ea_net_output_by_index(llava_ctx->depth_net, 0))[EA_W];
      llava_ctx->depth_rgb = ea_tensor_new(EA_U8, depth_rgb_shape, 0);
      EA_R_ASSERT(llava_ctx->depth_rgb != NULL);
    }
    llava_ctx->is_inited = 1;
  } while (0);

  return rval;
}

static int runVit(llava_ctx_t * llava_ctx, uint32_t out_file_num)
{
  int rval = EA_SUCCESS;
  struct shepd_reset_cfg reset_param;
  struct llava_onevision_extra * llava_ov_ex = NULL;
  struct llava_extra * llava_ex = NULL;
  EA_MEASURE_TIME_DECLARE();
  llava_params_t * params = llava_ctx->params;

  do {
    reset_param.reset_type = RESET_TYPE_HARD;
    if (shepherd_user_reset(llava_ctx->model_handle, llava_ctx->usr_ctx, &reset_param)) {
      printf("llava user context reset err\n");
      break;
    }
    llava_ctx->sr_enable = 0;
    llava_ctx->depth_enable = 0;
    llava_ov_ex = &llava_ctx->llm_cfg.shepd_ex.llava_onevision_ex;
    llava_ex = &llava_ctx->llm_cfg.shepd_ex.llava_ex;
    if (llava_ctx->vit_type == SINGLE) {
      if (params->llm_mode == LLAVA) {
        llava_ex->vit_in.img_num = 1;
        llava_ex->vit_in.img_mem.size = ea_tensor_size(llava_ctx->single_vit_img);
        llava_ex->vit_in.img_mem.virt = ea_tensor_data(llava_ctx->single_vit_img);
        llava_ex->vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->single_vit_img);
        llava_ex->vit_in.internal_cavalry_mem = 0;
      } else {
        llava_ov_ex->index = 1;
        llava_ov_ex->vit[1].vit_in.img_num = 1;
        llava_ov_ex->vit[1].vit_in.img_mem.size = ea_tensor_size(llava_ctx->single_vit_img);
        llava_ov_ex->vit[1].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->single_vit_img);
        llava_ov_ex->vit[1].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->single_vit_img);
        llava_ov_ex->vit[1].vit_in.internal_cavalry_mem = 0;
      }

    } else if (llava_ctx->vit_type == MULTI) {
      llava_ov_ex->index = 0;
      llava_ov_ex->vit[0].vit_in.img_num = out_file_num;
      llava_ov_ex->vit[0].vit_in.img_mem.size = ea_tensor_size(llava_ctx->vit_img) /
                                                ea_tensor_shape(llava_ctx->vit_img)[EA_N] *
                                                out_file_num;
      llava_ov_ex->vit[0].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->vit_img);
      llava_ov_ex->vit[0].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->vit_img);
      llava_ov_ex->vit[0].vit_in.internal_cavalry_mem = 0;
    } else if (llava_ctx->vit_type == VIDEO) {
      llava_ov_ex->index = 1;
      llava_ov_ex->vit[1].vit_in.img_num = out_file_num;
      llava_ov_ex->vit[1].vit_in.img_mem.size = ea_tensor_size(llava_ctx->vit_img) /
                                                ea_tensor_shape(llava_ctx->vit_img)[EA_N] *
                                                out_file_num;
      llava_ov_ex->vit[1].vit_in.img_mem.virt = ea_tensor_data(llava_ctx->vit_img);
      llava_ov_ex->vit[1].vit_in.img_mem.phys = ea_tensor_phys(llava_ctx->vit_img);
      llava_ov_ex->vit[1].vit_in.internal_cavalry_mem = 0;
    } else {
      // pass
    }
    EA_MEASURE_TIME_START();
    EA_R_OK(shepherd_user_preprocess(
      llava_ctx->model_handle,
      llava_ctx->usr_ctx,
      NULL,
      &llava_ctx->llm_cfg.shepd_ex,
      NULL));
    llava_ctx->vit_perf = EA_MEASURE_TIME_US();
  } while (0);

  return rval;
}

static int preprocessCvImage(
  llava_ctx_t * llava_ctx,
  uint8_t crop_to_square,
  const cv::Mat & cv_image,
  uint32_t * out_file_num)
{
  ea_io_image_preprocess_t prep;
  int rval = EA_SUCCESS;
  ea_tensor_t * orig = NULL;
  *out_file_num = 0;

  do {
    if (cv_image.empty()) {
      rval = EA_FAIL;
      break;
    }

    // Save OpenCV image to temporary file and use ea_io_get_tensor_from_image
    // This ensures the same image processing pipeline as the original code
    std::string temp_filename =
      "/tmp/temp_image_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jpg";

    if (!cv::imwrite(temp_filename, cv_image)) {
      printf("Failed to save temporary image file: %s\n", temp_filename.c_str());
      rval = EA_FAIL;
      break;
    }

    // Use the same method as preprocess_file_image
    orig = ea_io_get_tensor_from_image(temp_filename.c_str());
    if (orig == NULL) {
      printf("Failed to create tensor from temporary image file: %s\n", temp_filename.c_str());
      // Clean up temp file
      remove(temp_filename.c_str());
      rval = EA_FAIL;
      break;
    }

    memset(&prep, 0, sizeof(prep));
    prep.src_color = EA_TENSOR_COLOR_MODE_BGR;
    prep.dst_color = EA_TENSOR_COLOR_MODE_RGB;
    prep.crop_to_square = crop_to_square;
    prep.device = llava_ctx->device;

    if (llava_ctx->vit_type == SINGLE) {
      EA_R_OK(ea_io_image_preprocess(orig, llava_ctx->single_vit_img, &prep));
      *out_file_num = 1;
    } else {
      EA_R_OK(ea_io_image_preprocess(orig, llava_ctx->vit_imgs[0], &prep));
      *out_file_num = 1;
    }

    // Clean up
    if (orig) {
      ea_io_release_tensor_from_image(orig);
      orig = NULL;
    }

    // Remove temporary file
    remove(temp_filename.c_str());

  } while (0);

  return rval;
}

// Public interface functions
llava_ctx_t * llavaNetNew(llava_params_t * llava_params)
{
  (void)ea_log_level_local;  // suppress warning

  // Create application context
  llava_ctx_t * llava_ctx = new llava_ctx_t();

  // Set params pointer
  llava_ctx->params = llava_params;

  // Initialize demo (EazyAI environment and models)
  int ret = initDemo(llava_ctx, llava_params);
  if (ret != EA_SUCCESS) {
    printf("Failed to initialize LLaVA demo\n");
    llavaNetFree(llava_ctx);
    delete llava_ctx;
    return nullptr;
  }

  // Create user context
  llava_ctx->usr_ctx = shepherd_user_create(llava_ctx->model_handle);
  if (llava_ctx->usr_ctx == nullptr) {
    printf("Failed to create user context\n");
    llavaNetFree(llava_ctx);
    delete llava_ctx;
    return nullptr;
  }

  // Initialize stream callback to nullptr
  llava_ctx->stream_callback = nullptr;

  // Initialize image and question members
  llava_ctx->current_image = cv::Mat();
  llava_ctx->current_question = "";

  llava_ctx->initialized = true;
  return llava_ctx;
}

void llavaNetFree(llava_ctx_t * llava_ctx)
{
  if (!llava_ctx) {
    return;
  }

  if (llava_ctx->usr_ctx && llava_ctx->model_handle) {
    if (shepherd_user_release(llava_ctx->model_handle, llava_ctx->usr_ctx)) {
      printf("llava user release err\n");
    }
    llava_ctx->usr_ctx = nullptr;
  }

  deinitDemo(llava_ctx);
}

int processImage(llava_ctx_t * llava_ctx)
{
  if (!llava_ctx || !llava_ctx->initialized || llava_ctx->current_image.empty()) {
    return EA_FAIL;
  }

  int rval = EA_SUCCESS;
  uint32_t file_num = 0;

  do {
    uint8_t crop_to_square = 1;  // Default to crop to square

    // Process the OpenCV image
    rval = preprocessCvImage(llava_ctx, crop_to_square, llava_ctx->current_image, &file_num);
    if (rval != EA_SUCCESS || file_num == 0) {
      break;
    }

    // Run VIT processing after image preprocessing
    rval = runVit(llava_ctx, file_num);
    if (rval != EA_SUCCESS) {
      break;
    }

  } while (0);

  return rval;
}

int runLlavaInference(llava_ctx_t * llava_ctx)
{
  if (!llava_ctx || !llava_ctx->initialized) {
    printf("LLaVA not initialized. Call llavaNetNew() first.\n");
    return EA_FAIL;
  }

  llava_params_t * params = llava_ctx->params;

  int rval = EA_SUCCESS;
  struct shepd_run_cfg run_cfg;
  struct shepd_reset_cfg reset_param;
  struct shepd_output llm_output;
  struct token_id_list id_list;
  struct tokenizer_enc_cfg enc_cfg;
  struct tokenizer_dec_cfg dec_cfg;
  struct tokenizer_dec_res dec_res;
  uint32_t pos = 0;
  uint32_t last_token_id = 0;
  std::string labels = "";
  std::string user_input_with_label;
  std::string accumulated_response = "";
  uint32_t * ids = NULL;
  uint32_t ids_num = 0;
  char user_input_with_label_buf[MAX_USER_BUFF_SIZE];
  bool first_inference = true;  // Add flag to track first inference
  struct perf_ctx start, input, output;

  memset(&run_cfg, 0, sizeof(run_cfg));
  memset(&reset_param, 0, sizeof(reset_param));
  memset(&llm_output, 0, sizeof(llm_output));
  memset(&id_list, 0, sizeof(id_list));
  memset(&enc_cfg, 0, sizeof(enc_cfg));
  memset(&dec_cfg, 0, sizeof(dec_cfg));
  memset(&dec_res, 0, sizeof(dec_res));

  do {
    run_cfg.sample_hw_type = SAMPLER_HW_TYPE_NVP;

    // Prepare image labels for LLaVA OneVision
    if (params->llm_mode == LLAVA_OV) {
      if (llava_ctx->vit_type == VIDEO) {
        labels += "<image> ";
      } else {
        labels += "<image> ";  // Single image for now
      }
    }

    // Construct the query with labels
    user_input_with_label = labels + llava_ctx->current_question;

    // Ensure buffer size safety
    if (user_input_with_label.length() >= MAX_USER_BUFF_SIZE) {
      EA_LOG_ERROR("User input with label too long, will truncate.\n");
    }

    strncpy(user_input_with_label_buf, user_input_with_label.c_str(), MAX_USER_BUFF_SIZE - 1);
    user_input_with_label_buf[MAX_USER_BUFF_SIZE - 1] = '\0';

    // Tokenize the input
    perfRecord(&start, pos);
    if (shepherd_user_tokenizer_encode(
          llava_ctx->model_handle,
          llava_ctx->usr_ctx,
          &enc_cfg,
          user_input_with_label_buf,
          &id_list)) {
      EA_LOG_ERROR("llava run token encoding err.\n");
      rval = EA_FAIL;
      break;
    }
    ids = id_list.ids;
    ids_num = id_list.num;

    // Run inference loop
    while (true) {
      if (
        shepherd_user_run_ids(
          llava_ctx->model_handle,
          llava_ctx->usr_ctx,
          &run_cfg,
          ids,
          ids_num,
          &llm_output) < 0) {
        EA_LOG_ERROR("llava run ids err.\n");
        rval = EA_FAIL;
        break;
      }
      pos = llm_output.pos;

      // Only record input performance after first inference
      if (first_inference) {
        perfRecord(&input, pos);
        first_inference = false;
      }

      last_token_id = llm_output.token_id;

      if (llm_output.token_id && llm_output.token_id != llava_ctx->llm_cfg.eos_token_id) {
        if (
          shepherd_user_tokenizer_decode(
            llava_ctx->model_handle,
            llava_ctx->usr_ctx,
            &dec_cfg,
            &llm_output.token_id,
            1,
            &dec_res) < 0) {
          EA_LOG_ERROR("llava token decoding err.\n");
          rval = EA_FAIL;
          break;
        }

        // Accumulate response
        if (dec_res.len > 0) {
          std::string token_text = std::string(dec_res.text, dec_res.len);
          accumulated_response += token_text;

          // Send stream response if callback provided
          if (llava_ctx->stream_callback) {
            llava_ctx->stream_callback(token_text.c_str());
          }
        }
      }

      if (
        last_token_id == llava_ctx->llm_cfg.eos_token_id ||
        llm_output.pos >= llava_ctx->llm_cfg.max_seq_length - 1) {
        break;
      }

      if (llm_output.pos > llava_ctx->llm_cfg.max_seq_length - POS_MARGIN) {
        printf("Hit reset point, reset...\n");
        reset_param.reset_type = RESET_TYPE_HARD;
        if (shepherd_user_reset(llava_ctx->model_handle, llava_ctx->usr_ctx, &reset_param)) {
          printf("llava user context reset err\n");
          rval = EA_FAIL;
          break;
        }
        pos = reset_param.reset_pos;
      }
    }
    perfRecord(&output, pos);

    // printf("accumulated_response: %s\n", accumulated_response.c_str());

    //set performance
    llava_ctx->performance.vit_perf = (float)llava_ctx->vit_perf / (float)1000000;
    llava_ctx->performance.input_perf = perfTimeReport(&start, &input);
    llava_ctx->performance.output_perf = perfTimeReport(&input, &output);
    llava_ctx->performance.prefill_tokens = perfSpeedReport(&start, &input);
    llava_ctx->performance.generate_tokens = perfSpeedReport(&input, &output);

  } while (0);

  return rval;
}

llava_performance_t * getPerformance(llava_ctx_t * llava_ctx)
{
  if (llava_ctx) {
    return &llava_ctx->performance;
  }
  return nullptr;
}

llava_stream_callback_t * getStreamCallbackPtr(llava_ctx_t * llava_ctx)
{
  if (llava_ctx) {
    return &llava_ctx->stream_callback;
  }
  return nullptr;
}

void * getCurrentQuestionPtr(llava_ctx_t * llava_ctx)
{
  if (llava_ctx) {
    return &llava_ctx->current_question;
  }
  return nullptr;
}

void * getCurrentImagePtr(llava_ctx_t * llava_ctx)
{
  if (llava_ctx) {
    return &llava_ctx->current_image;
  }
  return nullptr;
}
