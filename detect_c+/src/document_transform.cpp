#include "document_detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace docdet {
namespace {

double distanceOf(const cv::Point2f& a, const cv::Point2f& b) {
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

// Recover the true width/height ratio of a rectangle from its perspective
// projection (Zhang & He, "Whiteboard scanning and image enhancement"),
// assuming the principal point is at the image centre. This corrects the
// foreshortening that makes a tilted document come out squished if the output
// size is taken from the projected edge lengths. Returns <= 0 when the
// estimate is degenerate (near fronto-parallel), so the caller can fall back.
double recoverAspectRatio(const std::array<cv::Point2f, 4>& c, const cv::Size& img) {
    const double u0 = img.width * 0.5;
    const double v0 = img.height * 0.5;
    // corners are ordered TL, TR, BR, BL -> Zhang-He labels m1..m4.
    const cv::Vec3d m1(c[0].x, c[0].y, 1.0);  // top-left
    const cv::Vec3d m2(c[1].x, c[1].y, 1.0);  // top-right
    const cv::Vec3d m3(c[3].x, c[3].y, 1.0);  // bottom-left
    const cv::Vec3d m4(c[2].x, c[2].y, 1.0);  // bottom-right

    const double den2 = m2.cross(m4).dot(m3);
    const double den3 = m3.cross(m4).dot(m2);
    if (std::abs(den2) < 1e-9 || std::abs(den3) < 1e-9) {
        return -1.0;
    }
    const double k2 = m1.cross(m4).dot(m3) / den2;
    const double k3 = m1.cross(m4).dot(m2) / den3;

    const cv::Vec3d n2 = k2 * m2 - m1;
    const cv::Vec3d n3 = k3 * m3 - m1;
    const double n23 = n2[2];
    const double n33 = n3[2];
    if (std::abs(n23) < 1e-9 || std::abs(n33) < 1e-9) {
        return -1.0;  // fronto-parallel: edge lengths are already correct
    }

    const double f2 =
        -((n2[0] * n3[0] - (n2[0] * n33 + n23 * n3[0]) * u0 + n23 * n33 * u0 * u0) +
          (n2[1] * n3[1] - (n2[1] * n33 + n23 * n3[1]) * v0 + n23 * n33 * v0 * v0)) /
        (n23 * n33);
    if (!(f2 > 0.0)) {
        return -1.0;
    }
    const double f = std::sqrt(f2);

    const cv::Matx33d A(f, 0, u0, 0, f, v0, 0, 0, 1);
    const cv::Matx33d Ai = A.inv();
    const cv::Matx33d AtA = Ai.t() * Ai;
    const double num = cv::Vec3d(AtA * n2).dot(n2);
    const double den = cv::Vec3d(AtA * n3).dot(n3);
    if (!(num > 0.0) || !(den > 0.0)) {
        return -1.0;
    }
    return std::sqrt(num / den);  // width / height
}

}  // namespace

cv::Mat warpDocument(const cv::Mat& image, const std::array<cv::Point2f, 4>& corners, int max_output_side) {
    const double width_a = distanceOf(corners[2], corners[3]);
    const double width_b = distanceOf(corners[1], corners[0]);
    const double height_a = distanceOf(corners[1], corners[2]);
    const double height_b = distanceOf(corners[0], corners[3]);

    const double naive_w = std::max(width_a, width_b);
    const double naive_h = std::max(height_a, height_b);

    // Use the perspective-recovered aspect ratio so a tilted document keeps its
    // true proportions; fall back to the projected edge lengths if degenerate.
    double ratio = recoverAspectRatio(corners, image.size());  // width / height
    if (!(ratio > 0.0) || !std::isfinite(ratio)) {
        ratio = naive_w / std::max(naive_h, 1.0);
    }

    // Size the output to roughly the largest projected edge to preserve detail,
    // while enforcing the recovered ratio.
    const double target = std::max(naive_w, naive_h);
    int out_w;
    int out_h;
    if (ratio >= 1.0) {
        out_w = static_cast<int>(std::round(target));
        out_h = static_cast<int>(std::round(target / ratio));
    } else {
        out_h = static_cast<int>(std::round(target));
        out_w = static_cast<int>(std::round(target * ratio));
    }
    out_w = std::max(1, out_w);
    out_h = std::max(1, out_h);

    const int longest = std::max(out_w, out_h);
    if (longest > max_output_side && max_output_side > 0) {
        const double scale = static_cast<double>(max_output_side) / longest;
        out_w = std::max(1, static_cast<int>(std::round(out_w * scale)));
        out_h = std::max(1, static_cast<int>(std::round(out_h * scale)));
    }

    std::array<cv::Point2f, 4> dst = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(out_w - 1), 0.0f),
        cv::Point2f(static_cast<float>(out_w - 1), static_cast<float>(out_h - 1)),
        cv::Point2f(0.0f, static_cast<float>(out_h - 1)),
    };

    const cv::Mat matrix = cv::getPerspectiveTransform(corners.data(), dst.data());
    cv::Mat warped;
    cv::warpPerspective(image, warped, matrix, cv::Size(out_w, out_h), cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    return warped;
}

}  // namespace docdet
