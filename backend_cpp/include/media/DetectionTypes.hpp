#ifndef MEDIA_DETECTION_TYPES_HPP
#define MEDIA_DETECTION_TYPES_HPP

#include <string>
#include <vector>

struct DetectionBox {
    float x1{0.0F};
    float y1{0.0F};
    float x2{0.0F};
    float y2{0.0F};
    float score{0.0F};
    int classId{0};
    std::string label;
};

using DetectionBoxes = std::vector<DetectionBox>;

#endif // MEDIA_DETECTION_TYPES_HPP
