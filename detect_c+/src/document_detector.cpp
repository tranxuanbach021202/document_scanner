#include "document_detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <numeric>

namespace docdet {
namespace {

double distanceOf(const cv::Point2f& a, const cv::Point2f& b) {
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

double polygonArea(const std::array<cv::Point2f, 4>& points) {
    double area = 0.0;
    for (int i = 0; i < 4; ++i) {
        const auto& p = points[i];
        const auto& q = points[(i + 1) % 4];
        area += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    return std::abs(area) * 0.5;
}

double areaRatio(const std::array<cv::Point2f, 4>& corners, const cv::Size& size) {
    const double image_area = static_cast<double>(size.width) * size.height;
    return polygonArea(corners) / std::max(image_area, 1.0);
}

double angleCosine(const cv::Point2f& a, const cv::Point2f& b, const cv::Point2f& c) {
    const cv::Point2f ab = a - b;
    const cv::Point2f cb = c - b;
    const double dot = static_cast<double>(ab.x) * cb.x + static_cast<double>(ab.y) * cb.y;
    const double len = std::sqrt(static_cast<double>(ab.x) * ab.x + static_cast<double>(ab.y) * ab.y) *
                       std::sqrt(static_cast<double>(cb.x) * cb.x + static_cast<double>(cb.y) * cb.y);
    if (len <= 1e-6) {
        return 1.0;
    }
    return std::abs(dot / len);
}

double rectangleScore(const std::array<cv::Point2f, 4>& corners, const cv::Size& size) {
    const double area_ratio = areaRatio(corners, size);

    double angle_penalty = 0.0;
    for (int i = 0; i < 4; ++i) {
        angle_penalty += angleCosine(corners[(i + 3) % 4], corners[i], corners[(i + 1) % 4]);
    }
    angle_penalty /= 4.0;

    const double top = distanceOf(corners[0], corners[1]);
    const double right = distanceOf(corners[1], corners[2]);
    const double bottom = distanceOf(corners[2], corners[3]);
    const double left = distanceOf(corners[3], corners[0]);
    const double width_balance = std::min(top, bottom) / std::max(std::max(top, bottom), 1.0);
    const double height_balance = std::min(left, right) / std::max(std::max(left, right), 1.0);

    // Saturate the area reward: past 60% of the frame extra area earns 4x
    // less, so "bigger" can no longer outvote contrast/edge evidence (a quad
    // snapped to table seams used to win purely on area).
    const double area_score = std::min(area_ratio, 0.60) * 2.0 +
                              std::max(0.0, area_ratio - 0.60) * 0.5;

    return area_score + width_balance * 0.2 + height_balance * 0.2 - angle_penalty * 0.35;
}

int borderTouchCount(const std::array<cv::Point2f, 4>& corners, const cv::Size& size) {
    const float margin = static_cast<float>(std::max(size.width, size.height)) * 0.015f;
    int touching = 0;
    for (const auto& p : corners) {
        if (p.x <= margin || p.y <= margin ||
            p.x >= static_cast<float>(size.width - 1) - margin ||
            p.y >= static_cast<float>(size.height - 1) - margin) {
            touching++;
        }
    }
    return touching;
}

bool touchesImageBorder(const std::array<cv::Point2f, 4>& corners, const cv::Size& size) {
    const int touching = borderTouchCount(corners, size);
    return touching >= 2;
}

bool candidateLooksLikeWholeImage(const std::array<cv::Point2f, 4>& corners, const cv::Size& size) {
    return areaRatio(corners, size) > 0.86 || borderTouchCount(corners, size) >= 3;
}

std::array<cv::Point2f, 4> expandCorners(
    const std::array<cv::Point2f, 4>& corners,
    const cv::Size& size,
    float amount) {
    cv::Point2f center(0.0f, 0.0f);
    for (const auto& p : corners) {
        center += p;
    }
    center *= 0.25f;

    std::array<cv::Point2f, 4> expanded = corners;
    for (auto& p : expanded) {
        p += (p - center) * amount;
        p.x = std::clamp(p.x, 0.0f, static_cast<float>(size.width - 1));
        p.y = std::clamp(p.y, 0.0f, static_cast<float>(size.height - 1));
    }
    return orderCornersClockwise(expanded);
}

cv::Point2f lineIntersection(const cv::Vec4i& a, const cv::Vec4i& b, bool& ok) {
    const float x1 = static_cast<float>(a[0]);
    const float y1 = static_cast<float>(a[1]);
    const float x2 = static_cast<float>(a[2]);
    const float y2 = static_cast<float>(a[3]);
    const float x3 = static_cast<float>(b[0]);
    const float y3 = static_cast<float>(b[1]);
    const float x4 = static_cast<float>(b[2]);
    const float y4 = static_cast<float>(b[3]);

    const float den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(den) < 1e-3f) {
        ok = false;
        return {};
    }

    const float p1 = x1 * y2 - y1 * x2;
    const float p2 = x3 * y4 - y3 * x4;
    ok = true;
    return {
        (p1 * (x3 - x4) - (x1 - x2) * p2) / den,
        (p1 * (y3 - y4) - (y1 - y2) * p2) / den,
    };
}

cv::Point2f polarLineIntersection(const cv::Vec2f& a, const cv::Vec2f& b, bool& ok) {
    const float rho1 = a[0];
    const float theta1 = a[1];
    const float rho2 = b[0];
    const float theta2 = b[1];
    const float c1 = std::cos(theta1);
    const float s1 = std::sin(theta1);
    const float c2 = std::cos(theta2);
    const float s2 = std::sin(theta2);
    const float det = c1 * s2 - s1 * c2;
    if (std::abs(det) < 1e-4f) {
        ok = false;
        return {};
    }
    ok = true;
    return {
        (s2 * rho1 - s1 * rho2) / det,
        (-c2 * rho1 + c1 * rho2) / det,
    };
}

float lineLength(const cv::Vec4i& line) {
    const float dx = static_cast<float>(line[2] - line[0]);
    const float dy = static_cast<float>(line[3] - line[1]);
    return std::sqrt(dx * dx + dy * dy);
}

float lineCenterX(const cv::Vec4i& line) {
    return (static_cast<float>(line[0]) + line[2]) * 0.5f;
}

float lineCenterY(const cv::Vec4i& line) {
    return (static_cast<float>(line[1]) + line[3]) * 0.5f;
}

struct FitLine {
    cv::Point2f point;
    cv::Point2f dir;
    bool valid = false;
};

cv::Point2f fitLineIntersection(const FitLine& a, const FitLine& b, bool& ok) {
    const float den = a.dir.x * b.dir.y - a.dir.y * b.dir.x;
    if (!a.valid || !b.valid || std::abs(den) < 1e-4f) {
        ok = false;
        return {};
    }
    const cv::Point2f delta = b.point - a.point;
    const float t = (delta.x * b.dir.y - delta.y * b.dir.x) / den;
    ok = true;
    return a.point + a.dir * t;
}

FitLine fitPoints(const std::vector<cv::Point2f>& points) {
    FitLine line;
    if (points.size() < 12) {
        return line;
    }
    cv::Vec4f fitted;
    cv::fitLine(points, fitted, cv::DIST_HUBER, 0, 0.01, 0.01);
    line.dir = {fitted[0], fitted[1]};
    line.point = {fitted[2], fitted[3]};
    line.valid = true;
    return line;
}

cv::Mat resizeForDetection(const cv::Mat& src, int max_side, double& scale) {
    const int max_dim = std::max(src.cols, src.rows);
    if (max_dim <= max_side) {
        scale = 1.0;
        return src.clone();
    }
    scale = static_cast<double>(max_side) / max_dim;
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(), scale, scale, cv::INTER_AREA);
    return dst;
}

cv::Mat toGray(const cv::Mat& image) {
    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else if (image.channels() == 4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    } else {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    return gray;
}

// Bilinear sample of a single-channel 8-bit image with border clamping.
float sampleGray(const cv::Mat& gray, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(gray.cols - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(gray.rows - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, gray.cols - 1);
    const int y1 = std::min(y0 + 1, gray.rows - 1);
    const float fx = x - x0;
    const float fy = y - y0;
    const float v00 = gray.at<uchar>(y0, x0);
    const float v01 = gray.at<uchar>(y0, x1);
    const float v10 = gray.at<uchar>(y1, x0);
    const float v11 = gray.at<uchar>(y1, x1);
    const float top = v00 + (v01 - v00) * fx;
    const float bottom = v10 + (v11 - v10) * fx;
    return top + (bottom - top) * fy;
}

// Edge-support score: how strongly the 4 quad sides sit on a *real document
// border*. A genuine paper edge has the bright document on the inside and a
// darker background on the outside; an inner structure (an ornamental printed
// frame, a text block) has bright paper on *both* sides. We therefore reward a
// directional step (inside brighter than outside) rather than any step, and
// sample across a wider band so the measure reflects the broad paper<->bg
// contrast instead of latching onto a thin printed line.
double edgeSupportScore(const std::array<cv::Point2f, 4>& corners, const cv::Mat& gray) {
    const int samples = 24;
    const float offset = std::max(6.0f, std::min(gray.cols, gray.rows) * 0.012f);
    const cv::Point2f center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    std::array<double, 4> side_support = {0.0, 0.0, 0.0, 0.0};
    for (int e = 0; e < 4; ++e) {
        const cv::Point2f a = corners[e];
        const cv::Point2f b = corners[(e + 1) % 4];
        cv::Point2f dir = b - a;
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-3f) {
            continue;
        }
        dir *= 1.0f / len;
        const cv::Point2f normal(-dir.y, dir.x);
        double side = 0.0;
        for (int i = 0; i < samples; ++i) {
            const float t = (i + 0.5f) / samples;
            const cv::Point2f p = a + (b - a) * t;
            // Orient the normal so "inside" points toward the quad centre.
            const cv::Point2f to_center = center - p;
            const float s = (normal.x * to_center.x + normal.y * to_center.y) >= 0.0f ? 1.0f : -1.0f;
            const cv::Point2f n = normal * s;
            const float in = sampleGray(gray, p.x + n.x * offset, p.y + n.y * offset);
            const float out = sampleGray(gray, p.x - n.x * offset, p.y - n.y * offset);
            // Mostly reward the directional step (paper inside, darker bg
            // outside); keep a small symmetric term so a reverse-polarity real
            // edge (a dark document on a bright desk) still registers, while an
            // inner frame (bright on both sides) stays near zero.
            const float diff = in - out;
            side += 0.75 * std::max(0.0f, diff) + 0.25 * std::abs(diff);
        }
        side_support[e] = (side / samples) / 255.0;
    }
    // A real document has all four sides on a genuine bright-inside border, so
    // emphasise the *weakest* side. This rejects both inner printed frames
    // (all sides ~0) and a large background object such as a chair/table (at
    // least one side faces a brighter surface), which a 4-side average would
    // not separate from the true paper quad.
    const double min_side = std::min({side_support[0], side_support[1], side_support[2], side_support[3]});
    const double mean_side = (side_support[0] + side_support[1] + side_support[2] + side_support[3]) / 4.0;
    return 0.6 * min_side + 0.4 * mean_side;  // normalized to [0, 1]
}

// Ring-contrast score: (inside - outside) mean luma difference in [-1, 1].
// A real paper sheet is brighter than the surrounding surface ring; a quad
// whose sides sit on table seams has the *same* material inside and outside,
// so its ring contrast is ~0. This is the global counterpart to the local
// band used by edgeSupportScore.
double ringContrastScore(
    const std::array<cv::Point2f, 4>& corners,
    const cv::Mat& gray,
    double inside_luma) {
    const int samples = 24;
    const float ring_off = std::max(10.0f, std::min(gray.cols, gray.rows) * 0.035f);
    const cv::Point2f center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;

    double outer_sum = 0.0;
    int valid = 0;
    for (int e = 0; e < 4; ++e) {
        const cv::Point2f a = corners[e];
        const cv::Point2f b = corners[(e + 1) % 4];
        cv::Point2f dir = b - a;
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-3f) {
            continue;
        }
        dir *= 1.0f / len;
        const cv::Point2f normal(-dir.y, dir.x);
        for (int i = 0; i < samples; ++i) {
            const float t = (i + 0.5f) / samples;
            const cv::Point2f p = a + (b - a) * t;
            const cv::Point2f to_center = center - p;
            const float s = (normal.x * to_center.x + normal.y * to_center.y) >= 0.0f ? 1.0f : -1.0f;
            const cv::Point2f q = p - normal * (s * ring_off);  // outward
            // Skip samples that fall off the image *before* clamping —
            // clamped samples would fake ~0 contrast for near-border docs.
            if (q.x < 0.0f || q.y < 0.0f ||
                q.x > static_cast<float>(gray.cols - 1) ||
                q.y > static_cast<float>(gray.rows - 1)) {
                continue;
            }
            outer_sum += sampleGray(gray, q.x, q.y);
            valid++;
        }
    }
    // Too few outer samples inside the frame (quad hugs the border): neutral.
    if (valid < 4 * samples * 2 / 5) {
        return 0.0;
    }
    return inside_luma - (outer_sum / valid) / 255.0;
}

// Local gradient-magnitude evidence at p: max central-difference gradient in
// a tiny cross of lateral offsets around the probe. Measuring raw gradient
// (instead of the thresholded Canny map) keeps this sensitive to faint seams
// that sit below the main Canny thresholds; the lateral offsets catch thin
// ridge-like grooves whose very centre has near-zero gradient.
float gradientEvidence(const cv::Mat& gray, const cv::Point2f& p, const cv::Point2f& normal) {
    float best = 0.0f;
    for (int n = -2; n <= 2; n += 2) {
        const float x = p.x + normal.x * n;
        const float y = p.y + normal.y * n;
        if (x < 1.5f || y < 1.5f ||
            x > static_cast<float>(gray.cols) - 2.5f ||
            y > static_cast<float>(gray.rows) - 2.5f) {
            continue;  // off-image counts as "no evidence"
        }
        const float gx = sampleGray(gray, x + 1.5f, y) - sampleGray(gray, x - 1.5f, y);
        const float gy = sampleGray(gray, x, y + 1.5f) - sampleGray(gray, x, y - 1.5f);
        best = std::max(best, std::sqrt(gx * gx + gy * gy));
    }
    return best;
}

// Fraction [0, 1] of quad-side ends whose supporting image structure keeps
// going *past* the corner. A paper border terminates at the paper's corners; a
// table seam or plank edge runs straight through them. Each end compares the
// mean gradient evidence along the side's extension (5–20% of the side length
// beyond the corner) against the side's own evidence: a seam extends with
// similar strength, while beyond a real document corner there is only flat
// background.
double edgeOvershootPenalty(
    const std::array<cv::Point2f, 4>& corners,
    const cv::Mat& gray) {
    const int side_samples = 12;
    const int probes = 8;
    int overshooting_ends = 0;
    for (int e = 0; e < 4; ++e) {
        const cv::Point2f a = corners[e];
        const cv::Point2f b = corners[(e + 1) % 4];
        cv::Point2f dir = b - a;
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-3f) {
            continue;
        }
        dir *= 1.0f / len;
        const cv::Point2f normal(-dir.y, dir.x);

        // The side's own evidence, sampled over its middle 60% (corners often
        // blur/round off, so keep clear of the ends).
        double side_ev = 0.0;
        for (int i = 0; i < side_samples; ++i) {
            const float t = 0.2f + 0.6f * (i + 0.5f) / side_samples;
            side_ev += gradientEvidence(gray, a + (b - a) * t, normal);
        }
        side_ev /= side_samples;

        // end = 0 probes beyond b (forward), end = 1 probes beyond a (backward).
        for (int end = 0; end < 2; ++end) {
            const cv::Point2f origin = end == 0 ? b : a;
            const float sign = end == 0 ? 1.0f : -1.0f;
            double ext_ev = 0.0;
            for (int i = 0; i < probes; ++i) {
                const float dist = len * (0.05f + 0.15f * (i + 0.5f) / probes);
                ext_ev += gradientEvidence(gray, origin + dir * (sign * dist), normal);
            }
            ext_ev /= probes;
            // Overshoot: the extension carries comparable evidence to the side
            // itself, and is clearly above the sensor-noise floor.
            if (ext_ev > 5.0 && ext_ev > 0.55 * side_ev) {
                overshooting_ends++;
            }
        }
    }
    return overshooting_ends / 8.0;
}

double quadrilateralScore(const std::array<cv::Point2f, 4>& corners, const cv::Mat& gray) {
    const cv::Size size = gray.size();
    const double area_ratio = areaRatio(corners, size);
    if (area_ratio < 0.10 || area_ratio > 0.91) {
        return -1e9;
    }

    std::vector<cv::Point> poly;
    poly.reserve(4);
    for (const auto& p : corners) {
        if (p.x < -size.width * 0.08f || p.y < -size.height * 0.08f ||
            p.x > size.width * 1.08f || p.y > size.height * 1.08f) {
            return -1e9;
        }
        poly.emplace_back(cv::Point(
            std::clamp(cvRound(p.x), 0, size.width - 1),
            std::clamp(cvRound(p.y), 0, size.height - 1)));
    }
    if (!cv::isContourConvex(poly)) {
        return -1e9;
    }

    const double top = distanceOf(corners[0], corners[1]);
    const double right = distanceOf(corners[1], corners[2]);
    const double bottom = distanceOf(corners[2], corners[3]);
    const double left = distanceOf(corners[3], corners[0]);
    const double min_side = std::min({top, right, bottom, left});
    if (min_side < std::min(size.width, size.height) * 0.18) {
        return -1e9;
    }

    cv::Mat mask = cv::Mat::zeros(size, CV_8UC1);
    cv::fillConvexPoly(mask, poly, cv::Scalar(255));
    const double inside_luma = cv::mean(gray, mask)[0] / 255.0;

    const cv::Point2f center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    const double center_dx = std::abs(center.x - size.width * 0.5) / size.width;
    const double center_dy = std::abs(center.y - size.height * 0.5) / size.height;
    const double center_score = 1.0 - std::min(1.0, center_dx + center_dy);
    const int border_touches = borderTouchCount(corners, size);
    const double border_penalty = border_touches >= 2 ? 0.65 : (border_touches == 1 ? 0.18 : 0.0);

    const double edge_support = edgeSupportScore(corners, gray);
    const double ring_contrast = ringContrastScore(corners, gray, inside_luma);
    const double overshoot = edgeOvershootPenalty(corners, gray);

    // A large candidate whose sides are not all on a genuine bright-inside
    // document border (a chair/table, the whole frame, or an inner printed
    // frame) must not win on area alone. Support quality blends the local
    // edge steps with the global ring contrast so a seam-backed quad (strong
    // edge steps but ~0 ring contrast) cannot buy off the area penalty.
    const double edge_quality = std::min(1.0, edge_support / 0.45);
    const double ring_quality = std::clamp(ring_contrast / 0.12, 0.0, 1.0);
    const double support_quality = 0.7 * edge_quality + 0.3 * ring_quality;
    const double weak_area_penalty = (1.0 - support_quality) * area_ratio * 1.6;

    // Reward paper-brighter-than-surroundings; capped so easy high-contrast
    // scenes keep roughly the current score scale (Dart threshold stays 1.5).
    const double ring_bonus = std::clamp(ring_contrast, -0.25, 0.25) * 0.9;
    // Penalise sides whose supporting lines continue past the corners (table
    // seams). Dead zone 0.30 forgives incidental clutter beyond one corner.
    const double overshoot_penalty = 0.8 * std::max(0.0, overshoot - 0.30) / 0.70;

    return rectangleScore(corners, size) + inside_luma * 0.55 + center_score * 0.35 +
           edge_support * 1.2 + ring_bonus - overshoot_penalty -
           border_penalty - weak_area_penalty;
}

void scaleResult(DetectionResult& result, double inv_scale) {
    for (auto& point : result.corners) {
        point.x *= static_cast<float>(inv_scale);
        point.y *= static_cast<float>(inv_scale);
    }
}

void considerCandidate(
    DetectionResult& best,
    const std::array<cv::Point2f, 4>& raw_corners,
    const cv::Mat& gray,
    const std::string& source) {
    std::array<cv::Point2f, 4> corners = orderCornersClockwise(raw_corners);
    const double score = quadrilateralScore(corners, gray);
    if (score < -1e8) {
        return;
    }
    if (!best.found || score > best.score) {
        best.found = true;
        best.corners = corners;
        best.score = score;
        best.reason = source;
    }
}

std::array<cv::Point2f, 4> refineBoxWithEdges(
    const cv::Mat& gray,
    const std::array<cv::Point2f, 4>& corners) {
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);

    cv::Mat grad_x;
    cv::Mat grad_y;
    cv::Sobel(blurred, grad_x, CV_32F, 1, 0, 3);
    cv::Sobel(blurred, grad_y, CV_32F, 0, 1, 3);
    cv::absdiff(grad_x, cv::Scalar(0), grad_x);
    cv::absdiff(grad_y, cv::Scalar(0), grad_y);

    const cv::Rect bounds = cv::boundingRect(std::vector<cv::Point2f>(corners.begin(), corners.end()));
    const int pad_x = std::max(20, bounds.width / 8);
    const int pad_y = std::max(20, bounds.height / 8);
    const int x0 = std::max(0, bounds.x - pad_x);
    const int x1 = std::min(gray.cols - 1, bounds.x + bounds.width + pad_x);
    const int y0 = std::max(0, bounds.y - pad_y);
    const int y1 = std::min(gray.rows - 1, bounds.y + bounds.height + pad_y);

    std::vector<cv::Point2f> top_points;
    std::vector<cv::Point2f> bottom_points;
    std::vector<cv::Point2f> left_points;
    std::vector<cv::Point2f> right_points;

    const int step = 4;
    for (int x = x0; x <= x1; x += step) {
        int best_top_y = y0;
        int best_bottom_y = y1;
        float best_top = 0.0f;
        float best_bottom = 0.0f;
        for (int y = y0; y <= std::min(y1, bounds.y + pad_y); ++y) {
            const float v = grad_y.at<float>(y, x);
            if (v > best_top) {
                best_top = v;
                best_top_y = y;
            }
        }
        for (int y = std::max(y0, bounds.y + bounds.height - pad_y); y <= y1; ++y) {
            const float v = grad_y.at<float>(y, x);
            if (v > best_bottom) {
                best_bottom = v;
                best_bottom_y = y;
            }
        }
        if (best_top > 18.0f) {
            top_points.emplace_back(static_cast<float>(x), static_cast<float>(best_top_y));
        }
        if (best_bottom > 18.0f) {
            bottom_points.emplace_back(static_cast<float>(x), static_cast<float>(best_bottom_y));
        }
    }

    for (int y = y0; y <= y1; y += step) {
        int best_left_x = x0;
        int best_right_x = x1;
        float best_left = 0.0f;
        float best_right = 0.0f;
        for (int x = x0; x <= std::min(x1, bounds.x + pad_x); ++x) {
            const float v = grad_x.at<float>(y, x);
            if (v > best_left) {
                best_left = v;
                best_left_x = x;
            }
        }
        for (int x = std::max(x0, bounds.x + bounds.width - pad_x); x <= x1; ++x) {
            const float v = grad_x.at<float>(y, x);
            if (v > best_right) {
                best_right = v;
                best_right_x = x;
            }
        }
        if (best_left > 18.0f) {
            left_points.emplace_back(static_cast<float>(best_left_x), static_cast<float>(y));
        }
        if (best_right > 18.0f) {
            right_points.emplace_back(static_cast<float>(best_right_x), static_cast<float>(y));
        }
    }

    const FitLine top = fitPoints(top_points);
    const FitLine bottom = fitPoints(bottom_points);
    const FitLine left = fitPoints(left_points);
    const FitLine right = fitPoints(right_points);

    bool ok_tl = false;
    bool ok_tr = false;
    bool ok_br = false;
    bool ok_bl = false;
    std::array<cv::Point2f, 4> refined = {
        fitLineIntersection(top, left, ok_tl),
        fitLineIntersection(top, right, ok_tr),
        fitLineIntersection(bottom, right, ok_br),
        fitLineIntersection(bottom, left, ok_bl),
    };

    if (!(ok_tl && ok_tr && ok_br && ok_bl)) {
        return corners;
    }
    for (auto& p : refined) {
        p.x = std::clamp(p.x, 0.0f, static_cast<float>(gray.cols - 1));
        p.y = std::clamp(p.y, 0.0f, static_cast<float>(gray.rows - 1));
    }
    if (polygonArea(refined) < polygonArea(corners) * 0.55 ||
        polygonArea(refined) > polygonArea(corners) * 1.35) {
        return corners;
    }
    return orderCornersClockwise(refined);
}

std::array<cv::Point2f, 4> refineTopByContentGap(
    const cv::Mat& gray,
    const std::array<cv::Point2f, 4>& corners) {
    std::array<cv::Point2f, 4> ordered = orderCornersClockwise(corners);
    const cv::Rect bounds = cv::boundingRect(std::vector<cv::Point2f>(ordered.begin(), ordered.end()));
    if (bounds.width < gray.cols * 0.35 || bounds.height < gray.rows * 0.35) {
        return ordered;
    }

    const int x0 = std::clamp(bounds.x + static_cast<int>(bounds.width * 0.18), 0, gray.cols - 1);
    const int x1 = std::clamp(bounds.x + static_cast<int>(bounds.width * 0.82), x0 + 1, gray.cols);
    const int y0 = std::clamp(bounds.y + static_cast<int>(bounds.height * 0.04), 0, gray.rows - 1);
    const int y1 = std::clamp(bounds.y + static_cast<int>(bounds.height * 0.75), y0 + 1, gray.rows);

    std::vector<int> dark_counts;
    dark_counts.reserve(std::max(0, y1 - y0));
    int max_count = 0;
    for (int y = y0; y < y1; ++y) {
        int count = 0;
        const uchar* row = gray.ptr<uchar>(y);
        for (int x = x0; x < x1; ++x) {
            if (row[x] < 135) {
                count++;
            }
        }
        dark_counts.push_back(count);
        max_count = std::max(max_count, count);
    }
    if (max_count < std::max(10, (x1 - x0) / 30)) {
        return ordered;
    }

    const int threshold = std::max({8, (x1 - x0) / 45, static_cast<int>(max_count * 0.22)});
    int content_y = -1;
    for (size_t i = 0; i < dark_counts.size(); ++i) {
        if (dark_counts[i] >= threshold) {
            content_y = y0 + static_cast<int>(i);
            break;
        }
    }
    if (content_y < 0) {
        return ordered;
    }

    const float top_y = (ordered[0].y + ordered[1].y) * 0.5f;
    const float bottom_y = (ordered[2].y + ordered[3].y) * 0.5f;
    const float height = std::max(1.0f, bottom_y - top_y);
    const float gap = static_cast<float>(content_y) - top_y;
    if (gap < height * 0.18f) {
        return ordered;
    }

    const float target_y = std::max(top_y, static_cast<float>(content_y) - height * 0.04f);
    const float t = std::clamp((target_y - top_y) / height, 0.0f, 0.42f);
    if (t <= 0.04f) {
        return ordered;
    }

    ordered[0] = ordered[0] + (ordered[3] - ordered[0]) * t;
    ordered[1] = ordered[1] + (ordered[2] - ordered[1]) * t;
    return orderCornersClockwise(ordered);
}

// Preprocessing computed once per frame and shared across detectors to avoid
// recomputing grayscale / blur / edges in every detector.
struct Preprocessed {
    cv::Mat resized;   // detection-scale image (original channels)
    cv::Mat gray;      // single-channel grayscale
    cv::Mat blurred;   // gaussian-blurred gray
    cv::Mat edges;     // canny + morphology edge map
    mutable cv::Mat clahe_cache;  // CLAHE-equalized gray (computed lazily)

    const cv::Mat& clahe() const {
        if (clahe_cache.empty()) {
            cv::Ptr<cv::CLAHE> c = cv::createCLAHE(2.0, cv::Size(8, 8));
            c->apply(gray, clahe_cache);
        }
        return clahe_cache;
    }
};

cv::Mat buildEdgeMap(const cv::Mat& blurred, const DetectionOptions& options) {
    cv::Mat edges;
    cv::Canny(blurred, edges, options.canny_low, options.canny_high);

    const int kernel_size = std::max(3, options.morph_kernel | 1);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernel_size, kernel_size));
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);
    cv::dilate(edges, edges, kernel, cv::Point(-1, -1), 1);
    return edges;
}

Preprocessed buildPreprocessed(const cv::Mat& resized, const DetectionOptions& options) {
    Preprocessed pre;
    pre.resized = resized;
    pre.gray = toGray(resized);
    cv::GaussianBlur(pre.gray, pre.blurred, cv::Size(5, 5), 0);
    pre.edges = buildEdgeMap(pre.blurred, options);
    return pre;
}

DetectionResult detectByContours(const Preprocessed& pre, const DetectionOptions& options, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;
    const cv::Mat& edges = pre.edges;

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) > cv::contourArea(b);
    });

    DetectionResult best;
    const double min_area = static_cast<double>(resized.cols) * resized.rows * options.min_area_ratio;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < min_area) {
            break;
        }

        std::vector<cv::Point> approx;
        const double peri = cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, 0.02 * peri, true);

        std::array<cv::Point2f, 4> corners{};
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            corners = orderCornersClockwise(approx);
        } else {
            std::vector<cv::Point> hull;
            cv::convexHull(contour, hull);
            if (hull.size() < 4) {
                continue;
            }
            cv::RotatedRect rect = cv::minAreaRect(hull);
            cv::Point2f rect_points[4];
            rect.points(rect_points);
            corners = orderCornersClockwise(std::array<cv::Point2f, 4>{
                rect_points[0], rect_points[1], rect_points[2], rect_points[3]});
            if (candidateLooksLikeWholeImage(corners, resized.size())) {
                continue;
            }
        }

        const double quad_width =
            (distanceOf(corners[0], corners[1]) + distanceOf(corners[2], corners[3])) * 0.5;
        const double quad_height =
            (distanceOf(corners[1], corners[2]) + distanceOf(corners[3], corners[0])) * 0.5;
        corners = expandCorners(corners, resized.size(), quad_width > quad_height ? 0.025f : 0.006f);
        considerCandidate(best, corners, gray, "contour");
    }

    if (!best.found) {
        best.reason = "no quadrilateral contour";
        return best;
    }

    scaleResult(best, inv_scale);
    return best;
}

DetectionResult detectByForegroundContour(const Preprocessed& pre, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    cv::Mat bgr;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, bgr, cv::COLOR_GRAY2BGR);
    } else if (resized.channels() == 4) {
        cv::cvtColor(resized, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = resized.clone();
    }

    cv::Mat closed;
    const cv::Mat close_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(bgr, closed, cv::MORPH_CLOSE, close_kernel, cv::Point(-1, -1), 3);

    cv::Mat grab_mask(closed.size(), CV_8UC1, cv::Scalar(cv::GC_BGD));
    cv::Mat bgd_model;
    cv::Mat fgd_model;
    const int inset = std::max(12, std::min(resized.cols, resized.rows) / 45);
    cv::Rect rect(
        inset,
        inset,
        std::max(1, resized.cols - inset * 2),
        std::max(1, resized.rows - inset * 2));
    try {
        cv::grabCut(closed, grab_mask, rect, bgd_model, fgd_model, 4, cv::GC_INIT_WITH_RECT);
    } catch (const cv::Exception&) {
        DetectionResult result;
        result.reason = "grabcut failed";
        return result;
    }

    cv::Mat fg_mask = (grab_mask == cv::GC_FGD) | (grab_mask == cv::GC_PR_FGD);
    fg_mask.convertTo(fg_mask, CV_8UC1, 255);

    cv::Mat foreground = cv::Mat::zeros(closed.size(), closed.type());
    closed.copyTo(foreground, fg_mask);

    cv::Mat gray;
    cv::cvtColor(foreground, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(11, 11), 0);

    cv::Mat edges;
    cv::Canny(gray, edges, 0, 200);
    const cv::Mat edge_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::dilate(edges, edges, edge_kernel, cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);
    std::sort(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) > cv::contourArea(b);
    });

    DetectionResult best;
    const cv::Mat& score_gray = pre.gray;
    const double min_area = static_cast<double>(resized.cols) * resized.rows * 0.12;
    const size_t count = std::min<size_t>(contours.size(), 8);
    for (size_t i = 0; i < count; ++i) {
        const auto& contour = contours[i];
        if (cv::contourArea(contour) < min_area) {
            continue;
        }

        std::array<cv::Point2f, 4> corners{};
        bool has_quad = false;
        const double peri = cv::arcLength(contour, true);
        for (double epsilon_factor : {0.015, 0.02, 0.028, 0.04}) {
            std::vector<cv::Point> approx;
            cv::approxPolyDP(contour, approx, epsilon_factor * peri, true);
            if (approx.size() == 4 && cv::isContourConvex(approx)) {
                corners = orderCornersClockwise(approx);
                has_quad = true;
                break;
            }
        }

        if (!has_quad) {
            std::vector<cv::Point> hull;
            cv::convexHull(contour, hull);
            if (hull.size() < 4) {
                continue;
            }
            cv::RotatedRect rect_candidate = cv::minAreaRect(hull);
            cv::Point2f rect_points[4];
            rect_candidate.points(rect_points);
            corners = orderCornersClockwise(std::array<cv::Point2f, 4>{
                rect_points[0], rect_points[1], rect_points[2], rect_points[3]});
        }

        const double quad_width =
            (distanceOf(corners[0], corners[1]) + distanceOf(corners[2], corners[3])) * 0.5;
        const double quad_height =
            (distanceOf(corners[1], corners[2]) + distanceOf(corners[3], corners[0])) * 0.5;
        corners = expandCorners(corners, resized.size(), quad_width > quad_height ? 0.006f : 0.0f);

        if (areaRatio(corners, resized.size()) > 0.88 || borderTouchCount(corners, resized.size()) >= 3) {
            continue;
        }
        considerCandidate(best, corners, score_gray, "foreground-contour");
    }

    if (!best.found) {
        best.reason = "no foreground contour";
        return best;
    }

    scaleResult(best, inv_scale);
    return best;
}

DetectionResult detectByPaperMask(const Preprocessed& pre, const DetectionOptions& options, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;
    cv::Mat bgr;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, bgr, cv::COLOR_GRAY2BGR);
    } else if (resized.channels() == 4) {
        cv::cvtColor(resized, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = resized;
    }

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv, hsv_channels);

    cv::Mat low_saturation;
    cv::Mat bright;
    cv::threshold(hsv_channels[1], low_saturation, 82, 255, cv::THRESH_BINARY_INV);
    cv::threshold(hsv_channels[2], bright, 125, 255, cv::THRESH_BINARY);

    cv::Mat mask;
    cv::bitwise_and(low_saturation, bright, mask);

    cv::Mat lab;
    cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);
    std::vector<cv::Mat> lab_channels;
    cv::split(lab, lab_channels);

    cv::Mat light_lab;
    cv::Mat neutral_a_low;
    cv::Mat neutral_a_high;
    cv::Mat not_yellow;
    cv::threshold(lab_channels[0], light_lab, 132, 255, cv::THRESH_BINARY);
    cv::threshold(lab_channels[1], neutral_a_low, 108, 255, cv::THRESH_BINARY);
    cv::threshold(lab_channels[1], neutral_a_high, 148, 255, cv::THRESH_BINARY_INV);
    cv::threshold(lab_channels[2], not_yellow, 162, 255, cv::THRESH_BINARY_INV);

    cv::Mat lab_mask;
    cv::bitwise_and(light_lab, neutral_a_low, lab_mask);
    cv::bitwise_and(lab_mask, neutral_a_high, lab_mask);
    cv::bitwise_and(lab_mask, not_yellow, lab_mask);
    cv::bitwise_or(mask, lab_mask, mask);

    const cv::Mat close_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(17, 17));
    const cv::Mat open_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, close_kernel, cv::Point(-1, -1), 2);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, open_kernel, cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // The absolute HSV/Lab thresholds above fail when the paper lies on a
    // surface that is itself bright and low-saturation (a light wooden desk):
    // the mask then covers the whole frame and gets rejected. Add an adaptive
    // luma split (Otsu on the blurred gray) as a second candidate source — it
    // separates "brightest surface in the scene" (the paper) from a background
    // that is only slightly darker, which no fixed threshold can do.
    cv::Mat otsu_mask;
    cv::threshold(pre.blurred, otsu_mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::morphologyEx(otsu_mask, otsu_mask, cv::MORPH_CLOSE, close_kernel, cv::Point(-1, -1), 2);
    cv::morphologyEx(otsu_mask, otsu_mask, cv::MORPH_OPEN, open_kernel, cv::Point(-1, -1), 1);
    std::vector<std::vector<cv::Point>> otsu_contours;
    cv::findContours(otsu_mask, otsu_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    contours.insert(contours.end(), otsu_contours.begin(), otsu_contours.end());

    DetectionResult best;
    const double min_area = static_cast<double>(resized.cols) * resized.rows * options.min_area_ratio;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < min_area) {
            continue;
        }

        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);

        std::vector<cv::Point> approx;
        const double peri = cv::arcLength(hull, true);
        cv::approxPolyDP(hull, approx, 0.035 * peri, true);

        std::array<cv::Point2f, 4> corners{};
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            corners = orderCornersClockwise(approx);
        } else {
            cv::RotatedRect rect = cv::minAreaRect(hull);
            cv::Point2f rect_points[4];
            rect.points(rect_points);
            corners = orderCornersClockwise(std::array<cv::Point2f, 4>{
                rect_points[0], rect_points[1], rect_points[2], rect_points[3]});
        }

        if (candidateLooksLikeWholeImage(corners, resized.size())) {
            continue;
        }

        corners = expandCorners(corners, resized.size(), 0.015f);
        considerCandidate(best, corners, gray, "paper-mask");
    }

    if (!best.found) {
        best.reason = "no paper-like region";
        return best;
    }

    scaleResult(best, inv_scale);
    return best;
}

DetectionResult detectByPolarHough(const Preprocessed& pre, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;

    cv::Mat blurred;
    cv::GaussianBlur(pre.clahe(), blurred, cv::Size(5, 5), 0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 35, 120);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);

    std::vector<cv::Vec2f> lines;
    cv::HoughLines(edges, lines, 1, CV_PI / 180, std::max(90, std::min(resized.cols, resized.rows) / 7));
    if (lines.size() < 4) {
        cv::HoughLines(edges, lines, 1, CV_PI / 180, 60);
    }

    std::vector<cv::Vec2f> vertical;
    std::vector<cv::Vec2f> horizontal;
    for (const auto& line : lines) {
        const float theta = line[1];
        const float deg = theta * 180.0f / static_cast<float>(CV_PI);
        if (deg < 28.0f || deg > 152.0f) {
            vertical.push_back(line);
        } else if (deg > 58.0f && deg < 122.0f) {
            horizontal.push_back(line);
        }
    }

    DetectionResult best;
    if (vertical.size() < 2 || horizontal.size() < 2) {
        best.reason = "not enough polar hough lines";
        return best;
    }

    auto xAtCenter = [&](const cv::Vec2f& line) {
        const float rho = line[0];
        const float theta = line[1];
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        if (std::abs(c) < 1e-4f) {
            return resized.cols * 0.5f;
        }
        return (rho - resized.rows * 0.5f * s) / c;
    };
    auto yAtCenter = [&](const cv::Vec2f& line) {
        const float rho = line[0];
        const float theta = line[1];
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        if (std::abs(s) < 1e-4f) {
            return resized.rows * 0.5f;
        }
        return (rho - resized.cols * 0.5f * c) / s;
    };

    std::sort(vertical.begin(), vertical.end(), [&](const auto& a, const auto& b) {
        return xAtCenter(a) < xAtCenter(b);
    });
    std::sort(horizontal.begin(), horizontal.end(), [&](const auto& a, const auto& b) {
        return yAtCenter(a) < yAtCenter(b);
    });

    const size_t v_window = std::min<size_t>(vertical.size(), 16);
    const size_t h_window = std::min<size_t>(horizontal.size(), 16);
    const size_t v_right_start = vertical.size() > v_window ? vertical.size() - v_window : 0;
    const size_t h_bottom_start = horizontal.size() > h_window ? horizontal.size() - h_window : 0;

    for (size_t li = 0; li < v_window; ++li) {
        for (size_t ri = v_right_start; ri < vertical.size(); ++ri) {
            if (xAtCenter(vertical[ri]) - xAtCenter(vertical[li]) < resized.cols * 0.30f) {
                continue;
            }
            for (size_t ti = 0; ti < h_window; ++ti) {
                for (size_t bi = h_bottom_start; bi < horizontal.size(); ++bi) {
                    if (yAtCenter(horizontal[bi]) - yAtCenter(horizontal[ti]) < resized.rows * 0.30f) {
                        continue;
                    }

                    bool ok_tl = false;
                    bool ok_tr = false;
                    bool ok_br = false;
                    bool ok_bl = false;
                    std::array<cv::Point2f, 4> corners = {
                        polarLineIntersection(horizontal[ti], vertical[li], ok_tl),
                        polarLineIntersection(horizontal[ti], vertical[ri], ok_tr),
                        polarLineIntersection(horizontal[bi], vertical[ri], ok_br),
                        polarLineIntersection(horizontal[bi], vertical[li], ok_bl),
                    };
                    if (!(ok_tl && ok_tr && ok_br && ok_bl)) {
                        continue;
                    }

                    for (auto& p : corners) {
                        p.x = std::clamp(p.x, 0.0f, static_cast<float>(resized.cols - 1));
                        p.y = std::clamp(p.y, 0.0f, static_cast<float>(resized.rows - 1));
                    }
                    corners = orderCornersClockwise(corners);
                    if (borderTouchCount(corners, resized.size()) >= 2 ||
                        areaRatio(corners, resized.size()) > 0.82) {
                        continue;
                    }
                    considerCandidate(best, corners, gray, "polar-hough");
                }
            }
        }
    }

    if (!best.found) {
        best.reason = "no valid polar hough quadrilateral";
        return best;
    }
    scaleResult(best, inv_scale);
    return best;
}

DetectionResult detectByHoughLines(const Preprocessed& pre, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;

    cv::Mat blurred;
    cv::GaussianBlur(pre.clahe(), blurred, cv::Size(5, 5), 0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 30, 100);

    const cv::Mat close_kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, close_kernel, cv::Point(-1, -1), 1);

    std::vector<cv::Vec4i> lines;
    const int min_line = std::max(70, std::min(resized.cols, resized.rows) / 7);
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 38, min_line, 45);

    std::vector<cv::Vec4i> horizontal;
    std::vector<cv::Vec4i> vertical;
    for (const auto& line : lines) {
        const float dx = static_cast<float>(line[2] - line[0]);
        const float dy = static_cast<float>(line[3] - line[1]);
        if (lineLength(line) < min_line) {
            continue;
        }
        if (std::abs(dy) < std::abs(dx) * 0.45f) {
            horizontal.push_back(line);
        } else if (std::abs(dx) < std::abs(dy) * 0.70f) {
            vertical.push_back(line);
        }
    }

    DetectionResult result;
    if (horizontal.size() < 2) {
        result.reason = "not enough horizontal border lines";
        return result;
    }
    if (vertical.size() < 2) {
        result.reason = "not enough vertical border lines";
        return result;
    }

    std::sort(horizontal.begin(), horizontal.end(), [](const auto& a, const auto& b) {
        return lineCenterY(a) < lineCenterY(b);
    });
    std::sort(vertical.begin(), vertical.end(), [](const auto& a, const auto& b) {
        return lineCenterX(a) < lineCenterX(b);
    });

    const size_t h_count = std::min<size_t>(horizontal.size(), 14);
    const size_t v_count = std::min<size_t>(vertical.size(), 14);
    const double image_area = static_cast<double>(resized.cols) * resized.rows;

    DetectionResult best;
    for (size_t ti = 0; ti < h_count; ++ti) {
        for (size_t bi = horizontal.size() > h_count ? horizontal.size() - h_count : 0; bi < horizontal.size(); ++bi) {
            if (lineCenterY(horizontal[bi]) - lineCenterY(horizontal[ti]) < resized.rows * 0.30f) {
                continue;
            }
            for (size_t li = 0; li < v_count; ++li) {
                for (size_t ri = vertical.size() > v_count ? vertical.size() - v_count : 0; ri < vertical.size(); ++ri) {
                    if (lineCenterX(vertical[ri]) - lineCenterX(vertical[li]) < resized.cols * 0.30f) {
                        continue;
                    }

                    bool ok_tl = false;
                    bool ok_tr = false;
                    bool ok_br = false;
                    bool ok_bl = false;
                    std::array<cv::Point2f, 4> corners = {
                        lineIntersection(horizontal[ti], vertical[li], ok_tl),
                        lineIntersection(horizontal[ti], vertical[ri], ok_tr),
                        lineIntersection(horizontal[bi], vertical[ri], ok_br),
                        lineIntersection(horizontal[bi], vertical[li], ok_bl),
                    };
                    if (!(ok_tl && ok_tr && ok_br && ok_bl)) {
                        continue;
                    }
                    for (auto& p : corners) {
                        p.x = std::clamp(p.x, 0.0f, static_cast<float>(resized.cols - 1));
                        p.y = std::clamp(p.y, 0.0f, static_cast<float>(resized.rows - 1));
                    }

                    corners = orderCornersClockwise(corners);
                    const double area_ratio = polygonArea(corners) / std::max(image_area, 1.0);
                    if (area_ratio < 0.12 || area_ratio > 0.88 ||
                        borderTouchCount(corners, resized.size()) >= 2 ||
                        candidateLooksLikeWholeImage(corners, resized.size())) {
                        continue;
                    }

                    std::vector<cv::Point> poly;
                    for (const auto& p : corners) {
                        poly.emplace_back(cv::Point(cvRound(p.x), cvRound(p.y)));
                    }
                    cv::Mat poly_mask = cv::Mat::zeros(gray.size(), CV_8UC1);
                    cv::fillConvexPoly(poly_mask, poly, cv::Scalar(255));
                    const double inside_luma = cv::mean(gray, poly_mask)[0] / 255.0;

                    const cv::Point2f center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
                    const double center_dx = std::abs(center.x - resized.cols * 0.5) / resized.cols;
                    const double center_dy = std::abs(center.y - resized.rows * 0.5) / resized.rows;
                    const double center_bonus = 1.0 - std::min(1.0, center_dx + center_dy);

                    double score = quadrilateralScore(corners, gray);
                    if (score < -1e8) {
                        continue;
                    }
                    score += inside_luma * 0.2 + center_bonus * 0.15;
                    if (!best.found || score > best.score) {
                        best.found = true;
                        best.corners = corners;
                        best.score = score;
                    }
                }
            }
        }
    }

    if (!best.found) {
        result.reason = "no valid hough quadrilateral";
        return result;
    }

    result = best;
    for (auto& point : result.corners) {
        point.x *= static_cast<float>(inv_scale);
        point.y *= static_cast<float>(inv_scale);
    }
    return result;
}

DetectionResult detectByCentralLightBox(const Preprocessed& pre, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;
    cv::Mat bgr;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, bgr, cv::COLOR_GRAY2BGR);
    } else if (resized.channels() == 4) {
        cv::cvtColor(resized, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = resized;
    }

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);

    cv::Mat low_sat;
    cv::Mat bright;
    cv::threshold(channels[1], low_sat, 95, 255, cv::THRESH_BINARY_INV);
    cv::threshold(channels[2], bright, 95, 255, cv::THRESH_BINARY);

    cv::Mat mask;
    cv::bitwise_and(low_sat, bright, mask);

    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(25, 25));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 2);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const cv::Point2f image_center(resized.cols * 0.5f, resized.rows * 0.5f);
    DetectionResult best;
    for (const auto& contour : contours) {
        cv::Rect box = cv::boundingRect(contour);
        const double area_ratio = static_cast<double>(box.area()) /
                                  std::max(1.0, static_cast<double>(resized.cols) * resized.rows);
        if (area_ratio < 0.18 || area_ratio > 0.86) {
            continue;
        }
        if (!box.contains(cv::Point(cvRound(image_center.x), cvRound(image_center.y)))) {
            continue;
        }

        const int pad_x = static_cast<int>(box.width * 0.03);
        const int pad_y = static_cast<int>(box.height * 0.03);
        box.x = std::max(0, box.x - pad_x);
        box.y = std::max(0, box.y - pad_y);
        box.width = std::min(resized.cols - box.x, box.width + pad_x * 2);
        box.height = std::min(resized.rows - box.y, box.height + pad_y * 2);

        std::array<cv::Point2f, 4> corners = {
            cv::Point2f(static_cast<float>(box.x), static_cast<float>(box.y)),
            cv::Point2f(static_cast<float>(box.x + box.width - 1), static_cast<float>(box.y)),
            cv::Point2f(static_cast<float>(box.x + box.width - 1), static_cast<float>(box.y + box.height - 1)),
            cv::Point2f(static_cast<float>(box.x), static_cast<float>(box.y + box.height - 1)),
        };
        corners = refineBoxWithEdges(gray, corners);
        if (candidateLooksLikeWholeImage(corners, resized.size())) {
            continue;
        }

        considerCandidate(best, corners, gray, "central-light-box");
    }

    if (!best.found) {
        best.reason = "no central light box";
        return best;
    }

    for (auto& point : best.corners) {
        point.x *= static_cast<float>(inv_scale);
        point.y *= static_cast<float>(inv_scale);
    }
    return best;
}

DetectionResult detectByEdgeProjection(const Preprocessed& pre, double inv_scale) {
    const cv::Mat& resized = pre.resized;
    const cv::Mat& gray = pre.gray;
    const cv::Mat& blurred = pre.blurred;

    cv::Mat grad_x;
    cv::Mat grad_y;
    cv::Sobel(blurred, grad_x, CV_32F, 1, 0, 3);
    cv::Sobel(blurred, grad_y, CV_32F, 0, 1, 3);
    cv::absdiff(grad_x, cv::Scalar(0), grad_x);
    cv::absdiff(grad_y, cv::Scalar(0), grad_y);

    auto borderRow = [&](int y0, int y1, int x0, int x1, bool from_bottom) {
        int best_y = y0;
        double max_score = -1.0;
        std::vector<double> scores;
        scores.reserve(std::max(0, y1 - y0));
        for (int y = y0; y < y1; ++y) {
            cv::Scalar s = cv::sum(grad_y(cv::Rect(x0, y, x1 - x0, 1)));
            scores.push_back(s[0]);
            max_score = std::max(max_score, s[0]);
        }
        const double threshold = max_score * 0.42;
        if (from_bottom) {
            for (int i = static_cast<int>(scores.size()) - 1; i >= 0; --i) {
                if (scores[static_cast<size_t>(i)] >= threshold) {
                    return std::pair<int, double>(y0 + i, scores[static_cast<size_t>(i)]);
                }
            }
        } else {
            for (size_t i = 0; i < scores.size(); ++i) {
                if (scores[i] >= threshold) {
                    return std::pair<int, double>(y0 + static_cast<int>(i), scores[i]);
                }
            }
        }
        return std::pair<int, double>(best_y, max_score);
    };

    auto borderCol = [&](int x0, int x1, int y0, int y1, bool from_right) {
        int best_x = x0;
        double max_score = -1.0;
        std::vector<double> scores;
        scores.reserve(std::max(0, x1 - x0));
        for (int x = x0; x < x1; ++x) {
            cv::Scalar s = cv::sum(grad_x(cv::Rect(x, y0, 1, y1 - y0)));
            scores.push_back(s[0]);
            max_score = std::max(max_score, s[0]);
        }
        const double threshold = max_score * 0.42;
        if (from_right) {
            for (int i = static_cast<int>(scores.size()) - 1; i >= 0; --i) {
                if (scores[static_cast<size_t>(i)] >= threshold) {
                    return std::pair<int, double>(x0 + i, scores[static_cast<size_t>(i)]);
                }
            }
        } else {
            for (size_t i = 0; i < scores.size(); ++i) {
                if (scores[i] >= threshold) {
                    return std::pair<int, double>(x0 + static_cast<int>(i), scores[i]);
                }
            }
        }
        return std::pair<int, double>(best_x, max_score);
    };

    const int w = resized.cols;
    const int h = resized.rows;
    const int cx0 = static_cast<int>(w * 0.08);
    const int cx1 = static_cast<int>(w * 0.92);

    const auto [top, top_score] =
        borderRow(static_cast<int>(h * 0.03), static_cast<int>(h * 0.45), cx0, cx1, false);
    const auto [bottom, bottom_score] =
        borderRow(static_cast<int>(h * 0.55), static_cast<int>(h * 0.98), cx0, cx1, true);

    if (bottom - top < h * 0.30 || top_score <= 0.0 || bottom_score <= 0.0) {
        DetectionResult result;
        result.reason = "projection rows invalid";
        return result;
    }

    const int y0 = std::max(0, top - static_cast<int>(h * 0.03));
    const int y1 = std::min(h - 1, bottom + static_cast<int>(h * 0.03));
    const auto [left, left_score] =
        borderCol(static_cast<int>(w * 0.02), static_cast<int>(w * 0.45), y0, y1, false);
    const auto [right, right_score] =
        borderCol(static_cast<int>(w * 0.55), static_cast<int>(w * 0.98), y0, y1, true);

    DetectionResult result;
    if (right - left < w * 0.30 || left_score <= 0.0 || right_score <= 0.0) {
        result.reason = "projection columns invalid";
        return result;
    }

    std::array<cv::Point2f, 4> corners = {
        cv::Point2f(static_cast<float>(left), static_cast<float>(top)),
        cv::Point2f(static_cast<float>(right), static_cast<float>(top)),
        cv::Point2f(static_cast<float>(right), static_cast<float>(bottom)),
        cv::Point2f(static_cast<float>(left), static_cast<float>(bottom)),
    };
    corners = refineBoxWithEdges(gray, corners);
    corners = refineTopByContentGap(gray, corners);

    if (candidateLooksLikeWholeImage(corners, resized.size())) {
        result.reason = "projection candidate touches image border";
        return result;
    }

    result.found = true;
    result.corners = corners;
    result.score = quadrilateralScore(corners, gray);
    result.reason = "edge-projection";
    if (result.score < -1e8) {
        result.found = false;
        result.reason = "projection candidate invalid";
        return result;
    }
    for (auto& point : result.corners) {
        point.x *= static_cast<float>(inv_scale);
        point.y *= static_cast<float>(inv_scale);
    }
    return result;
}

// Tinh chỉnh 4 góc ở độ phân giải gốc (full-res). Detection chạy trên ảnh đã
// resize rồi scale ngược, nên sai số bị khuếch đại; cornerSubPix kéo mỗi góc về
// đúng giao điểm cạnh thật. Chỉ chấp nhận khi dịch chuyển nhỏ để tránh "nhảy"
// sang một góc mạnh khác gần đó.
void refineCornersSubpixel(const cv::Mat& image, std::array<cv::Point2f, 4>& corners) {
    const cv::Mat gray = toGray(image);
    std::vector<cv::Point2f> pts(corners.begin(), corners.end());
    for (auto& p : pts) {
        p.x = std::clamp(p.x, 0.0f, static_cast<float>(gray.cols - 1));
        p.y = std::clamp(p.y, 0.0f, static_cast<float>(gray.rows - 1));
    }

    const int win = std::clamp(std::max(gray.cols, gray.rows) / 120, 5, 21);
    const std::vector<cv::Point2f> original = pts;
    try {
        cv::cornerSubPix(
            gray, pts, cv::Size(win, win), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.05));
    } catch (const cv::Exception&) {
        return;
    }

    const float max_shift = static_cast<float>(win) * 1.5f;
    for (size_t i = 0; i < corners.size(); ++i) {
        if (distanceOf(pts[i], original[i]) <= max_shift) {
            corners[i] = pts[i];
        }
    }
}

}  // namespace

DetectionResult detectDocument(const cv::Mat& image, const DetectionOptions& options) {
    DetectionResult result;
    if (image.empty()) {
        result.reason = "empty image";
        return result;
    }

    double scale = 1.0;
    const cv::Mat resized = resizeForDetection(image, options.max_detect_size, scale);
    const double inv_scale = 1.0 / scale;
    const Preprocessed pre = buildPreprocessed(resized, options);

    DetectionResult best;
    bool early_done = false;
    auto considerResult = [&](DetectionResult candidate, double penalty = 0.0) {
        if (early_done || !candidate.found) {
            return;
        }
        candidate.score -= penalty;
        if (!best.found || candidate.score > best.score) {
            best = candidate;
        }
        // In VIDEO mode, accept a strong candidate immediately and skip the
        // remaining (heavier) detectors to keep the per-frame budget low.
        if (options.mode == DetectMode::VIDEO && best.score >= options.early_accept_score) {
            early_done = true;
        }
    };

    if (options.mode == DetectMode::VIDEO) {
        // Fast detectors first so VIDEO mode can early-exit before doing more
        // work. Paper-mask runs before edge-projection: it is the detector
        // built for "bright paper on a colored surface", so it should compete
        // for the early-accept slot before the projection heuristic.
        considerResult(detectByContours(pre, options, inv_scale));
        considerResult(detectByPaperMask(pre, options, inv_scale));
        considerResult(detectByEdgeProjection(pre, inv_scale), 0.65);
    } else {
        // PHOTO: all detectors are independent, so run them in parallel and
        // merge afterwards. Latency is then bounded by the slowest detector
        // (GrabCut) instead of the sum of all of them.
        pre.clahe();  // populate the lazy cache once before threads read it
        auto f_contour = std::async(std::launch::async, detectByContours, std::cref(pre), std::cref(options), inv_scale);
        auto f_edge = std::async(std::launch::async, detectByEdgeProjection, std::cref(pre), inv_scale);
        auto f_paper = std::async(std::launch::async, detectByPaperMask, std::cref(pre), std::cref(options), inv_scale);
        auto f_fg = std::async(std::launch::async, detectByForegroundContour, std::cref(pre), inv_scale);
        auto f_box = std::async(std::launch::async, detectByCentralLightBox, std::cref(pre), inv_scale);
        auto f_hough = std::async(std::launch::async, detectByHoughLines, std::cref(pre), inv_scale);
        auto f_polar = std::async(std::launch::async, detectByPolarHough, std::cref(pre), inv_scale);

        considerResult(f_contour.get());
        considerResult(f_edge.get(), 0.65);
        considerResult(f_paper.get());
        considerResult(f_fg.get(), 0.12);
        considerResult(f_box.get(), 0.25);
        considerResult(f_hough.get(), 0.35);
        considerResult(f_polar.get(), 0.65);
    }

    if (best.found) {
        if (options.subpixel_refine) {
            refineCornersSubpixel(image, best.corners);
        }
        return best;
    }

    result.reason = "no quadrilateral contour or paper-like region";
    return result;
}

}  // namespace docdet
