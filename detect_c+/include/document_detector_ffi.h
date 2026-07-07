#pragma once

#ifdef _WIN32
#ifdef DOCDET_FFI_BUILD
#define DOCDET_FFI_API __declspec(dllexport)
#else
#define DOCDET_FFI_API __declspec(dllimport)
#endif
#else
#define DOCDET_FFI_API __attribute__((visibility("default")))
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum DocdetStatus {
    DOCDET_OK = 0,
    DOCDET_INVALID_ARGUMENT = -1,
    DOCDET_IMAGE_READ_FAILED = -2,
    DOCDET_DOCUMENT_NOT_FOUND = -3,
    DOCDET_IMAGE_WRITE_FAILED = -4,
    DOCDET_EXCEPTION = -100
};

// Detection speed/accuracy mode (mirrors docdet::DetectMode).
enum DocdetMode {
    DOCDET_MODE_PHOTO = 0,
    DOCDET_MODE_VIDEO = 1
};

typedef struct DocdetDetectionOptions {
    int max_detect_size;
    double min_area_ratio;
    double canny_low;
    double canny_high;
    int morph_kernel;
    int use_hough_fallback;
    int mode;                   // DocdetMode: 0 = PHOTO, 1 = VIDEO
    double early_accept_score;  // VIDEO mode early-exit threshold
    int subpixel_refine;        // 1 = refine corners with cornerSubPix
} DocdetDetectionOptions;

DOCDET_FFI_API DocdetDetectionOptions docdet_default_options(void);

DOCDET_FFI_API int docdet_detect_file(
    const char* input_path,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score);

DOCDET_FFI_API int docdet_process_file(
    const char* input_path,
    const char* output_path,
    int black_white,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score);

// Prepare a captured still for the manual corner-adjustment screen:
//   1. Read `input_path` (EXIF orientation applied) and write an upright,
//      EXIF-free copy to `normalized_output` (used for both display and warp).
//   2. Detect the document on that upright image and return its 4 corners in
//      upright pixel space, plus the upright width/height.
// If no document is found, `out_corners_8` is filled with a ~10% inset rectangle
// so the user can still drag the handles. Returns DOCDET_OK unless reading or
// writing the image fails.
DOCDET_FFI_API int docdet_prepare_capture(
    const char* input_path,
    const char* normalized_output,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score,
    int* out_width,
    int* out_height);

// Warp + enhance using caller-supplied corners (no detection). `corners_8` is
// 4 (x, y) pairs in the pixel space of `input_path` (the normalized image).
DOCDET_FFI_API int docdet_warp_file(
    const char* input_path,
    const char* output_path,
    const float* corners_8,
    int black_white);

// Detect on an in-memory YUV_420_888 frame (Android CameraImage). The three
// planes may carry row/pixel strides exactly as reported by the camera plugin.
// Corners are written in the full-resolution (width x height) coordinate space.
DOCDET_FFI_API int docdet_detect_yuv420(
    const uint8_t* y_plane,
    const uint8_t* u_plane,
    const uint8_t* v_plane,
    int width,
    int height,
    int y_row_stride,
    int uv_row_stride,
    int uv_pixel_stride,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score);

// Detect on an in-memory packed 8-bit-per-channel frame.
//   channels = 4 -> RGBA (iOS/macOS camera), 3 -> RGB, 1 -> grayscale.
// row_stride is the number of bytes per image row (>= width * channels).
DOCDET_FFI_API int docdet_detect_rgba(
    const uint8_t* data,
    int width,
    int height,
    int row_stride,
    int channels,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score);

DOCDET_FFI_API const char* docdet_last_error(void);

#ifdef __cplusplus
}
#endif
