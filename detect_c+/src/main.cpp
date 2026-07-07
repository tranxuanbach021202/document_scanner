#include "document_detector.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <input-image> <output-image> [--bw] [--video]\n";
}

void drawCorners(cv::Mat& image, const std::array<cv::Point2f, 4>& corners) {
    const int min_side = std::max(1, std::min(image.cols, image.rows));
    const int line_width = std::max(1, min_side / 420);
    const int point_radius = std::max(3, min_side / 120);
    for (int i = 0; i < 4; ++i) {
        cv::line(image, corners[i], corners[(i + 1) % 4], cv::Scalar(0, 255, 0), line_width);
        cv::circle(image, corners[i], point_radius, cv::Scalar(0, 0, 255), -1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string input_path = argv[1];
    const std::string output_path = argv[2];
    bool black_white = false;
    bool video_mode = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bw") {
            black_white = true;
        } else if (arg == "--video") {
            video_mode = true;
        }
    }

    cv::Mat image = cv::imread(input_path, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "Failed to read image: " << input_path << "\n";
        return 1;
    }

    docdet::DetectionOptions options;
    options.mode = video_mode ? docdet::DetectMode::VIDEO : docdet::DetectMode::PHOTO;
    const auto t0 = std::chrono::steady_clock::now();
    docdet::DetectionResult result = docdet::detectDocument(image, options);
    const auto t1 = std::chrono::steady_clock::now();
    const double detect_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (!result.found) {
        std::cerr << "Document not found: " << result.reason << "\n";
        return 3;
    }

    cv::Mat warped = docdet::warpDocument(image, result.corners);
    cv::Mat enhanced = docdet::enhanceDocument(warped, black_white);

    if (!cv::imwrite(output_path, enhanced)) {
        std::cerr << "Failed to write image: " << output_path << "\n";
        return 1;
    }

    cv::Mat debug = image.clone();
    drawCorners(debug, result.corners);
    const std::string debug_path = output_path + ".debug.jpg";
    cv::imwrite(debug_path, debug);

    std::cout << "found=true\n";
    std::cout << "mode=" << (video_mode ? "video" : "photo") << "\n";
    std::cout << "detect_ms=" << detect_ms << "\n";
    std::cout << "score=" << result.score << "\n";
    std::cout << "source=" << result.reason << "\n";
    std::cout << "corners=";
    for (const auto& p : result.corners) {
        std::cout << "(" << p.x << "," << p.y << ")";
    }
    std::cout << "\n";
    std::cout << "output=" << output_path << "\n";
    std::cout << "debug=" << debug_path << "\n";
    return 0;
}
