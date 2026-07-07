#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <vector>

namespace docdet {

// Detection mode controls the speed/accuracy trade-off.
//  - PHOTO: run the full ensemble for maximum accuracy (single-shot capture).
//  - VIDEO: run only the fast detectors with early-exit for real-time preview.
enum class DetectMode {
    PHOTO = 0,
    VIDEO = 1,
};

struct DetectionOptions {
    int max_detect_size = 1280;
    double min_area_ratio = 0.08;
    double canny_low = 50.0;
    double canny_high = 150.0;
    int morph_kernel = 5;
    bool use_hough_fallback = true;

    // Speed/accuracy mode. VIDEO skips the heavy detectors (GrabCut, Hough).
    DetectMode mode = DetectMode::PHOTO;
    // In VIDEO mode, if a detector returns a score above this threshold the
    // pipeline returns immediately without running the remaining detectors.
    double early_accept_score = 1.45;
    // Refine the final corners on the full-resolution image with cornerSubPix.
    bool subpixel_refine = true;
};

struct DetectionResult {
    bool found = false;
    std::array<cv::Point2f, 4> corners{};
    double score = 0.0;
    std::string reason;
};

DetectionResult detectDocument(const cv::Mat& image, const DetectionOptions& options = {});

cv::Mat warpDocument(
    const cv::Mat& image,
    const std::array<cv::Point2f, 4>& corners,
    int max_output_side = 2400);

cv::Mat enhanceDocument(const cv::Mat& image, bool black_white = false);

std::array<cv::Point2f, 4> orderCornersClockwise(const std::vector<cv::Point>& contour);
std::array<cv::Point2f, 4> orderCornersClockwise(const std::array<cv::Point2f, 4>& points);

}  // namespace docdet
