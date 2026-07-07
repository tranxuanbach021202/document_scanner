#include "document_detector.h"

#include <algorithm>

namespace docdet {

std::array<cv::Point2f, 4> orderCornersClockwise(const std::vector<cv::Point>& contour) {
    std::array<cv::Point2f, 4> points{};
    for (int i = 0; i < 4; ++i) {
        points[i] = cv::Point2f(static_cast<float>(contour[i].x), static_cast<float>(contour[i].y));
    }
    return orderCornersClockwise(points);
}

std::array<cv::Point2f, 4> orderCornersClockwise(const std::array<cv::Point2f, 4>& points) {
    std::array<cv::Point2f, 4> ordered{};

    auto sum = [](const cv::Point2f& p) { return p.x + p.y; };
    auto diff = [](const cv::Point2f& p) { return p.x - p.y; };

    ordered[0] = *std::min_element(points.begin(), points.end(), [&](const auto& a, const auto& b) {
        return sum(a) < sum(b);
    });
    ordered[2] = *std::max_element(points.begin(), points.end(), [&](const auto& a, const auto& b) {
        return sum(a) < sum(b);
    });
    ordered[1] = *std::max_element(points.begin(), points.end(), [&](const auto& a, const auto& b) {
        return diff(a) < diff(b);
    });
    ordered[3] = *std::min_element(points.begin(), points.end(), [&](const auto& a, const auto& b) {
        return diff(a) < diff(b);
    });

    return ordered;
}

}  // namespace docdet
