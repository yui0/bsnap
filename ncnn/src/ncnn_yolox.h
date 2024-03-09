#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

FFI_PLUGIN_EXPORT typedef int yolox_err_t;

#define YOLOX_OK        0
#define YOLOX_ERROR    -1

FFI_PLUGIN_EXPORT struct YoloX {
  const char *model_path;   // path to model file
  const char *param_path;   // path to param file

  float nms_thresh;   // nms threshold
  float conf_thresh;  // threshold of bounding box prob
  float target_size;  // target image size after resize, might use 416 for small model
};

// ncnn::Mat::PixelType
FFI_PLUGIN_EXPORT enum PixelType {
  PIXEL_RGB = 1,
  PIXEL_BGR = 2,
  PIXEL_GRAY = 3,
  PIXEL_RGBA = 4,
  PIXEL_BGRA = 5,
};

FFI_PLUGIN_EXPORT struct Rect {
  float x;
  float y;
  float w;
  float h;
};

FFI_PLUGIN_EXPORT struct Object {
  int label;
  float prob;
  struct Rect rect;
};

FFI_PLUGIN_EXPORT struct DetectResult {
  int object_num;
  struct Object *object;
};

FFI_PLUGIN_EXPORT struct YoloX *yoloxCreate();
FFI_PLUGIN_EXPORT void yoloxDestroy(struct YoloX *yolox);

FFI_PLUGIN_EXPORT struct DetectResult *detectResultCreate();
FFI_PLUGIN_EXPORT void detectResultDestroy(struct DetectResult *result);

FFI_PLUGIN_EXPORT yolox_err_t detectWithImagePath(
    struct YoloX *yolox, const char *image_path, struct DetectResult *result);
FFI_PLUGIN_EXPORT yolox_err_t detectWithPixels(
    struct YoloX *yolox, const uint8_t *pixels, enum PixelType pixelType,
    int img_w, int img_h, struct DetectResult *result);

#ifdef __cplusplus
}
#endif
