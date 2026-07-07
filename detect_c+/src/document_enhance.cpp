#include "document_detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace docdet {

cv::Mat enhanceDocument(const cv::Mat& image, bool black_white) {
    if (image.empty()) {
        return {};
    }

    cv::Mat bgr;
    if (image.channels() == 4) {
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    } else if (image.channels() == 1) {
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else {
        bgr = image;
    }

    // "Magic Color": normalise illumination so the page becomes a clean, even
    // white and soft shadows disappear, while keeping content colour. For each
    // channel we estimate the paper background (a morphological close removes
    // the darker text/marks, leaving the page level), then divide the channel
    // by that background. Paper/paper -> 1 -> white; text/paper -> dark. This
    // is what removes the grey cast left by uneven lighting.
    std::vector<cv::Mat> channels;
    cv::split(bgr, channels);

    // Estimate the background on a downscaled copy for speed, then upscale.
    const int small_side = 480;
    const double scale =
        std::min(1.0, static_cast<double>(small_side) / std::max(bgr.cols, bgr.rows));
    const cv::Size small_size(
        std::max(1, cvRound(bgr.cols * scale)), std::max(1, cvRound(bgr.rows * scale)));
    const int k = std::max(3, (std::min(small_size.width, small_size.height) / 8) | 1);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));

    for (auto& c : channels) {
        cv::Mat bg;
        cv::resize(c, bg, small_size, 0, 0, cv::INTER_AREA);
        cv::morphologyEx(bg, bg, cv::MORPH_CLOSE, kernel);  // paper level (text removed)
        cv::GaussianBlur(bg, bg, cv::Size(0, 0), k * 0.5);
        cv::resize(bg, bg, c.size(), 0, 0, cv::INTER_LINEAR);

        cv::Mat cf;
        cv::Mat bgf;
        c.convertTo(cf, CV_32F);
        bg.convertTo(bgf, CV_32F);
        bgf += 1.0f;  // avoid divide-by-zero
        cv::Mat norm;
        cv::divide(cf, bgf, norm, 255.0);            // channel / background * 255
        cv::threshold(norm, norm, 255.0, 255.0, cv::THRESH_TRUNC);
        norm.convertTo(c, CV_8U);
    }

    cv::Mat enhanced;
    cv::merge(channels, enhanced);

    // Gentle contrast to deepen text against the whitened page.
    cv::addWeighted(enhanced, 1.15, cv::Mat::zeros(enhanced.size(), enhanced.type()), 0.0, -10.0, enhanced);

    if (!black_white) {
        return enhanced;
    }

    cv::Mat gray;
    cv::cvtColor(enhanced, gray, cv::COLOR_BGR2GRAY);
    cv::Mat bw;
    cv::adaptiveThreshold(gray, bw, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 31, 8);
    return bw;
}

}  // namespace docdet
