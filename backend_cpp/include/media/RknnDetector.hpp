#ifndef MEDIA_RKNN_DETECTOR_HPP
#define MEDIA_RKNN_DETECTOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/DetectionTypes.hpp"

namespace cv {
class Mat;
}

class RknnDetector {
public:
    struct Config {
        std::string modelPath;
        float confidenceThreshold{0.35F};
        float nmsThreshold{0.45F};
        int maxDetections{64};
    };

    RknnDetector();
    explicit RknnDetector(Config config);
    ~RknnDetector();

    bool initialize();
    void shutdown();
    bool isReady() const;
    DetectionBoxes infer(const cv::Mat& bgrFrame);
    std::string lastError() const;

private:
    struct Impl;

    Config config_;
    std::unique_ptr<Impl> impl_;
};

#endif // MEDIA_RKNN_DETECTOR_HPP
