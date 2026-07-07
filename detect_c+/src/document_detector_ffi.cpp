#include "document_detector_ffi.h"

#include "document_detector.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

thread_local std::string last_error;

docdet::DetectionOptions toCppOptions(const DocdetDetectionOptions* options) {
    docdet::DetectionOptions cpp;
    if (options == nullptr) {
        return cpp;
    }

    cpp.max_detect_size = options->max_detect_size;
    cpp.min_area_ratio = options->min_area_ratio;
    cpp.canny_low = options->canny_low;
    cpp.canny_high = options->canny_high;
    cpp.morph_kernel = options->morph_kernel;
    cpp.use_hough_fallback = options->use_hough_fallback != 0;
    cpp.mode = options->mode == DOCDET_MODE_VIDEO ? docdet::DetectMode::VIDEO
                                                  : docdet::DetectMode::PHOTO;
    cpp.early_accept_score = options->early_accept_score;
    cpp.subpixel_refine = options->subpixel_refine != 0;
    return cpp;
}

// Rebuild a contiguous BGR image from planar/semi-planar YUV_420_888 where the
// U/V planes may carry arbitrary row and pixel strides (as Android reports).
// `step` subsamples the frame on the fly (every `step`-th pixel): detection
// only needs ~1280px, so converting a downscaled frame avoids doing the full
// 4K YUV->BGR conversion every preview frame. The output is (width/step) x
// (height/step); callers scale detected corners back by `step`.
cv::Mat yuv420ToBgr(
    const uint8_t* y, const uint8_t* u, const uint8_t* v,
    int width, int height, int y_row_stride, int uv_row_stride, int uv_pixel_stride,
    int step) {
    if (step < 1) {
        step = 1;
    }
    const int ow = (width / step) & ~1;   // even output luma width
    const int oh = (height / step) & ~1;  // even output luma height
    if (ow < 2 || oh < 2) {
        return {};
    }
    const int cw = ow / 2;
    const int ch = oh / 2;
    std::vector<uint8_t> i420(static_cast<size_t>(ow) * oh +
                              2u * static_cast<size_t>(cw) * ch);
    uint8_t* dst_y = i420.data();
    uint8_t* dst_u = dst_y + static_cast<size_t>(ow) * oh;
    uint8_t* dst_v = dst_u + static_cast<size_t>(cw) * ch;

    for (int oy = 0; oy < oh; ++oy) {
        const uint8_t* srow = y + static_cast<size_t>(oy * step) * y_row_stride;
        uint8_t* drow = dst_y + static_cast<size_t>(oy) * ow;
        for (int ox = 0; ox < ow; ++ox) {
            drow[ox] = srow[ox * step];
        }
    }
    for (int ocy = 0; ocy < ch; ++ocy) {
        const uint8_t* su = u + static_cast<size_t>(ocy * step) * uv_row_stride;
        const uint8_t* sv = v + static_cast<size_t>(ocy * step) * uv_row_stride;
        uint8_t* du = dst_u + static_cast<size_t>(ocy) * cw;
        uint8_t* dv = dst_v + static_cast<size_t>(ocy) * cw;
        for (int ocx = 0; ocx < cw; ++ocx) {
            du[ocx] = su[(ocx * step) * uv_pixel_stride];
            dv[ocx] = sv[(ocx * step) * uv_pixel_stride];
        }
    }

    cv::Mat yuv(oh + oh / 2, ow, CV_8UC1, i420.data());
    cv::Mat bgr;
    cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);
    return bgr;  // deep data already owned by `bgr`
}

void writeCorners(const docdet::DetectionResult& result, float* out_corners_8) {
    if (out_corners_8 == nullptr) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        out_corners_8[i * 2] = result.corners[i].x;
        out_corners_8[i * 2 + 1] = result.corners[i].y;
    }
}

void writeScore(const docdet::DetectionResult& result, double* out_score) {
    if (out_score != nullptr) {
        *out_score = result.score;
    }
}

int detectImage(
    const cv::Mat& image,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score,
    docdet::DetectionResult& result) {
    result = docdet::detectDocument(image, toCppOptions(options));
    if (!result.found) {
        last_error = result.reason;
        return DOCDET_DOCUMENT_NOT_FOUND;
    }

    writeCorners(result, out_corners_8);
    writeScore(result, out_score);
    last_error.clear();
    return DOCDET_OK;
}

}  // namespace

extern "C" {

DocdetDetectionOptions docdet_default_options(void) {
    const docdet::DetectionOptions cpp;
    return {
        cpp.max_detect_size,
        cpp.min_area_ratio,
        cpp.canny_low,
        cpp.canny_high,
        cpp.morph_kernel,
        cpp.use_hough_fallback ? 1 : 0,
        cpp.mode == docdet::DetectMode::VIDEO ? DOCDET_MODE_VIDEO : DOCDET_MODE_PHOTO,
        cpp.early_accept_score,
        cpp.subpixel_refine ? 1 : 0,
    };
}

int docdet_detect_file(
    const char* input_path,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score) {
    try {
        if (input_path == nullptr || out_corners_8 == nullptr) {
            last_error = "input_path and out_corners_8 are required";
            return DOCDET_INVALID_ARGUMENT;
        }

        const cv::Mat image = cv::imread(input_path, cv::IMREAD_COLOR);
        if (image.empty()) {
            last_error = "failed to read image";
            return DOCDET_IMAGE_READ_FAILED;
        }

        docdet::DetectionResult result;
        return detectImage(image, options, out_corners_8, out_score, result);
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

int docdet_process_file(
    const char* input_path,
    const char* output_path,
    int black_white,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score) {
    try {
        if (input_path == nullptr || output_path == nullptr || out_corners_8 == nullptr) {
            last_error = "input_path, output_path, and out_corners_8 are required";
            return DOCDET_INVALID_ARGUMENT;
        }

        const cv::Mat image = cv::imread(input_path, cv::IMREAD_COLOR);
        if (image.empty()) {
            last_error = "failed to read image";
            return DOCDET_IMAGE_READ_FAILED;
        }

        docdet::DetectionResult result;
        const int status = detectImage(image, options, out_corners_8, out_score, result);
        if (status != DOCDET_OK) {
            return status;
        }

        const cv::Mat warped = docdet::warpDocument(image, result.corners);
        const cv::Mat enhanced = docdet::enhanceDocument(warped, black_white != 0);
        if (!cv::imwrite(output_path, enhanced)) {
            last_error = "failed to write image";
            return DOCDET_IMAGE_WRITE_FAILED;
        }

        last_error.clear();
        return DOCDET_OK;
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

int docdet_prepare_capture(
    const char* input_path,
    const char* normalized_output,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score,
    int* out_width,
    int* out_height) {
    try {
        if (input_path == nullptr || normalized_output == nullptr || out_corners_8 == nullptr) {
            last_error = "input_path, normalized_output, and out_corners_8 are required";
            return DOCDET_INVALID_ARGUMENT;
        }

        // imread applies EXIF orientation, giving an upright image.
        const cv::Mat image = cv::imread(input_path, cv::IMREAD_COLOR);
        if (image.empty()) {
            last_error = "failed to read image";
            return DOCDET_IMAGE_READ_FAILED;
        }
        if (!cv::imwrite(normalized_output, image)) {
            last_error = "failed to write normalized image";
            return DOCDET_IMAGE_WRITE_FAILED;
        }
        if (out_width != nullptr) {
            *out_width = image.cols;
        }
        if (out_height != nullptr) {
            *out_height = image.rows;
        }

        const docdet::DetectionResult result = docdet::detectDocument(image, toCppOptions(options));
        if (result.found) {
            writeCorners(result, out_corners_8);
            writeScore(result, out_score);
        } else {
            // Default to a ~10% inset rectangle so the user can drag the handles.
            const float mx = image.cols * 0.10f;
            const float my = image.rows * 0.10f;
            const float right = image.cols - 1 - mx;
            const float bottom = image.rows - 1 - my;
            const float def[8] = {mx, my, right, my, right, bottom, mx, bottom};
            for (int i = 0; i < 8; ++i) {
                out_corners_8[i] = def[i];
            }
            if (out_score != nullptr) {
                *out_score = 0.0;
            }
        }

        last_error.clear();
        return DOCDET_OK;
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

int docdet_warp_file(
    const char* input_path,
    const char* output_path,
    const float* corners_8,
    int black_white) {
    try {
        if (input_path == nullptr || output_path == nullptr || corners_8 == nullptr) {
            last_error = "input_path, output_path, and corners_8 are required";
            return DOCDET_INVALID_ARGUMENT;
        }

        const cv::Mat image = cv::imread(input_path, cv::IMREAD_COLOR);
        if (image.empty()) {
            last_error = "failed to read image";
            return DOCDET_IMAGE_READ_FAILED;
        }

        std::array<cv::Point2f, 4> corners;
        for (int i = 0; i < 4; ++i) {
            corners[i] = cv::Point2f(corners_8[i * 2], corners_8[i * 2 + 1]);
        }
        corners = docdet::orderCornersClockwise(corners);

        const cv::Mat warped = docdet::warpDocument(image, corners);
        const cv::Mat enhanced = docdet::enhanceDocument(warped, black_white != 0);
        if (!cv::imwrite(output_path, enhanced)) {
            last_error = "failed to write image";
            return DOCDET_IMAGE_WRITE_FAILED;
        }

        last_error.clear();
        return DOCDET_OK;
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

int docdet_detect_yuv420(
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
    double* out_score) {
    try {
        if (y_plane == nullptr || u_plane == nullptr || v_plane == nullptr ||
            out_corners_8 == nullptr || width < 2 || height < 2) {
            last_error = "invalid yuv420 arguments";
            return DOCDET_INVALID_ARGUMENT;
        }

        // Detection only needs ~1280px, so subsample the (possibly 4K) preview
        // frame during YUV->BGR instead of converting it at full resolution.
        const int detect_max = (options != nullptr && options->max_detect_size > 0)
                                   ? options->max_detect_size
                                   : 1280;
        const int step = std::max(1, static_cast<int>(std::lround(
                                         static_cast<double>(std::max(width, height)) /
                                         (detect_max * 1.15))));
        const cv::Mat image = yuv420ToBgr(
            y_plane, u_plane, v_plane, width, height,
            y_row_stride, uv_row_stride, uv_pixel_stride, step);
        if (image.empty()) {
            last_error = "failed to convert yuv420 frame";
            return DOCDET_IMAGE_READ_FAILED;
        }

        docdet::DetectionResult result;
        const int status = detectImage(image, options, out_corners_8, out_score, result);
        // Map corners from the subsampled frame back to full-frame coordinates.
        if (status == DOCDET_OK && step > 1) {
            for (int i = 0; i < 8; ++i) {
                out_corners_8[i] *= static_cast<float>(step);
            }
        }
        return status;
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

int docdet_detect_rgba(
    const uint8_t* data,
    int width,
    int height,
    int row_stride,
    int channels,
    const DocdetDetectionOptions* options,
    float* out_corners_8,
    double* out_score) {
    try {
        if (data == nullptr || out_corners_8 == nullptr || width < 2 || height < 2 ||
            (channels != 1 && channels != 3 && channels != 4)) {
            last_error = "invalid rgba arguments";
            return DOCDET_INVALID_ARGUMENT;
        }

        const int type = channels == 1 ? CV_8UC1 : (channels == 3 ? CV_8UC3 : CV_8UC4);
        const cv::Mat view(height, width, type, const_cast<uint8_t*>(data),
                           static_cast<size_t>(row_stride));
        cv::Mat image;
        if (channels == 4) {
            cv::cvtColor(view, image, cv::COLOR_RGBA2BGR);
        } else if (channels == 1) {
            image = view.clone();
        } else {
            cv::cvtColor(view, image, cv::COLOR_RGB2BGR);
        }

        docdet::DetectionResult result;
        return detectImage(image, options, out_corners_8, out_score, result);
    } catch (const std::exception& error) {
        last_error = error.what();
        return DOCDET_EXCEPTION;
    } catch (...) {
        last_error = "unknown exception";
        return DOCDET_EXCEPTION;
    }
}

const char* docdet_last_error(void) {
    return last_error.c_str();
}

}
