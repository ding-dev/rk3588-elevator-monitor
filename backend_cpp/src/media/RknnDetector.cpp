#include "media/RknnDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#ifdef ENABLE_RKNN_DETECTION
#include "rknn_api.h"
#endif

namespace {

struct LetterboxInfo {
    float scale{1.0F};
    int padX{0};
    int padY{0};
    int inputWidth{0};
    int inputHeight{0};
    int sourceWidth{0};
    int sourceHeight{0};
};

float intersectionOverUnion(const DetectionBox& lhs, const DetectionBox& rhs) {
    const float x1 = std::max(lhs.x1, rhs.x1);
    const float y1 = std::max(lhs.y1, rhs.y1);
    const float x2 = std::min(lhs.x2, rhs.x2);
    const float y2 = std::min(lhs.y2, rhs.y2);
    const float width = std::max(0.0F, x2 - x1);
    const float height = std::max(0.0F, y2 - y1);
    const float intersection = width * height;
    const float lhsArea = std::max(0.0F, lhs.x2 - lhs.x1) * std::max(0.0F, lhs.y2 - lhs.y1);
    const float rhsArea = std::max(0.0F, rhs.x2 - rhs.x1) * std::max(0.0F, rhs.y2 - rhs.y1);
    const float denominator = lhsArea + rhsArea - intersection;
    if (denominator <= std::numeric_limits<float>::epsilon()) {
        return 0.0F;
    }
    return intersection / denominator;
}

DetectionBoxes nonMaximumSuppression(const DetectionBoxes& input,
                                     float iouThreshold,
                                     int maxDetections) {
    std::vector<int> indices(input.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&input](int lhs, int rhs) {
        return input[static_cast<std::size_t>(lhs)].score >
               input[static_cast<std::size_t>(rhs)].score;
    });

    std::vector<bool> removed(input.size(), false);
    DetectionBoxes result;
    result.reserve(input.size());

    for (std::size_t i = 0; i < indices.size(); ++i) {
        const int index = indices[i];
        if (removed[static_cast<std::size_t>(index)]) {
            continue;
        }

        result.push_back(input[static_cast<std::size_t>(index)]);
        if (static_cast<int>(result.size()) >= maxDetections) {
            break;
        }

        for (std::size_t j = i + 1; j < indices.size(); ++j) {
            const int candidate = indices[j];
            if (removed[static_cast<std::size_t>(candidate)]) {
                continue;
            }
            if (input[static_cast<std::size_t>(candidate)].classId !=
                input[static_cast<std::size_t>(index)].classId) {
                continue;
            }
            if (intersectionOverUnion(input[static_cast<std::size_t>(index)],
                                      input[static_cast<std::size_t>(candidate)]) > iouThreshold) {
                removed[static_cast<std::size_t>(candidate)] = true;
            }
        }
    }

    return result;
}

std::string defaultLabelForClass(int classId) {
    return "cls_" + std::to_string(classId);
}

cv::Mat makeLetterboxedBgr(const cv::Mat& source, int width, int height, LetterboxInfo& info) {
    info.inputWidth = width;
    info.inputHeight = height;
    info.sourceWidth = source.cols;
    info.sourceHeight = source.rows;
    info.scale = std::min(static_cast<float>(width) / static_cast<float>(source.cols),
                          static_cast<float>(height) / static_cast<float>(source.rows));

    const int scaledWidth = std::max(1, static_cast<int>(std::round(source.cols * info.scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::round(source.rows * info.scale)));
    info.padX = (width - scaledWidth) / 2;
    info.padY = (height - scaledHeight) / 2;

    cv::Mat resized;
    cv::resize(source, resized, cv::Size(scaledWidth, scaledHeight), 0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(info.padX, info.padY, resized.cols, resized.rows)));
    return canvas;
}

DetectionBox scaleBoxBack(float x1,
                          float y1,
                          float x2,
                          float y2,
                          const LetterboxInfo& info,
                          float score,
                          int classId) {
    DetectionBox box;
    box.x1 = std::clamp((x1 - static_cast<float>(info.padX)) / info.scale,
                        0.0F,
                        static_cast<float>(info.sourceWidth - 1));
    box.y1 = std::clamp((y1 - static_cast<float>(info.padY)) / info.scale,
                        0.0F,
                        static_cast<float>(info.sourceHeight - 1));
    box.x2 = std::clamp((x2 - static_cast<float>(info.padX)) / info.scale,
                        0.0F,
                        static_cast<float>(info.sourceWidth - 1));
    box.y2 = std::clamp((y2 - static_cast<float>(info.padY)) / info.scale,
                        0.0F,
                        static_cast<float>(info.sourceHeight - 1));
    box.score = score;
    box.classId = classId;
    box.label = defaultLabelForClass(classId);
    return box;
}

} // namespace

struct RknnDetector::Impl {
#ifdef ENABLE_RKNN_DETECTION
    rknn_context context{0};
    rknn_input_output_num ioNum{};
    std::vector<rknn_tensor_attr> inputAttrs;
    std::vector<rknn_tensor_attr> outputAttrs;
    std::vector<unsigned char> modelData;
    bool ready{false};
    bool warnedUnsupportedOutput{false};
    std::string lastError;

    static std::string dimsToString(const rknn_tensor_attr& attr) {
        std::ostringstream stream;
        stream << "[";
        for (uint32_t i = 0; i < attr.n_dims; ++i) {
            if (i != 0U) {
                stream << ",";
            }
            stream << attr.dims[i];
        }
        stream << "]";
        return stream.str();
    }

    bool loadModel(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            lastError = "unable to open RKNN model: " + path;
            return false;
        }

        modelData.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (modelData.empty()) {
            lastError = "RKNN model file is empty: " + path;
            return false;
        }
        return true;
    }

    bool initialize(const Config& config) {
        if (!loadModel(config.modelPath)) {
            return false;
        }

        const int initResult = rknn_init(
            &context, modelData.data(), static_cast<uint32_t>(modelData.size()), 0, nullptr);
        if (initResult < 0) {
            lastError = "rknn_init failed: " + std::to_string(initResult);
            context = 0;
            return false;
        }

        rknn_set_core_mask(context, RKNN_NPU_CORE_0_1_2);

        if (rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &ioNum, sizeof(ioNum)) < 0) {
            lastError = "failed to query RKNN input/output count";
            return false;
        }
        if (ioNum.n_input == 0U || ioNum.n_output == 0U) {
            lastError = "RKNN model has invalid input/output count";
            return false;
        }

        inputAttrs.resize(ioNum.n_input);
        for (uint32_t i = 0; i < ioNum.n_input; ++i) {
            inputAttrs[i].index = i;
            if (rknn_query(context, RKNN_QUERY_INPUT_ATTR, &inputAttrs[i], sizeof(rknn_tensor_attr)) < 0) {
                lastError = "failed to query RKNN input attr " + std::to_string(i);
                return false;
            }
        }

        outputAttrs.resize(ioNum.n_output);
        for (uint32_t i = 0; i < ioNum.n_output; ++i) {
            outputAttrs[i].index = i;
            if (rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &outputAttrs[i], sizeof(rknn_tensor_attr)) < 0) {
                lastError = "failed to query RKNN output attr " + std::to_string(i);
                return false;
            }
        }

        ready = true;
        std::cout << "[RKNN] Model loaded from " << config.modelPath << std::endl;
        for (std::size_t i = 0; i < inputAttrs.size(); ++i) {
            std::cout << "[RKNN] Input[" << i << "] dims=" << dimsToString(inputAttrs[i]) << std::endl;
        }
        for (std::size_t i = 0; i < outputAttrs.size(); ++i) {
            std::cout << "[RKNN] Output[" << i << "] dims=" << dimsToString(outputAttrs[i]) << std::endl;
        }
        return true;
    }

    void shutdown() {
        if (context != 0) {
            rknn_destroy(context);
            context = 0;
        }
        inputAttrs.clear();
        outputAttrs.clear();
        modelData.clear();
        ready = false;
        warnedUnsupportedOutput = false;
    }

    std::pair<int, int> inputSize() const {
        if (inputAttrs.empty()) {
            return {0, 0};
        }

        const rknn_tensor_attr& attr = inputAttrs.front();
        if (attr.n_dims < 4U) {
            return {0, 0};
        }

        if (attr.fmt == RKNN_TENSOR_NCHW) {
            return {static_cast<int>(attr.dims[3]), static_cast<int>(attr.dims[2])};
        }
        return {static_cast<int>(attr.dims[2]), static_cast<int>(attr.dims[1])};
    }

    static std::vector<int> normalizeDims(const rknn_tensor_attr& attr) {
        std::vector<int> dims;
        dims.reserve(attr.n_dims);
        for (uint32_t i = 0; i < attr.n_dims; ++i) {
            dims.push_back(static_cast<int>(attr.dims[i]));
        }
        while (!dims.empty() && dims.front() == 1) {
            dims.erase(dims.begin());
        }
        return dims;
    }

    DetectionBoxes decodeSingleOutput(const float* data,
                                      std::size_t count,
                                      const rknn_tensor_attr& attr,
                                      const LetterboxInfo& info,
                                      const Config& config) {
        const std::vector<int> dims = normalizeDims(attr);
        if (dims.size() != 2U) {
            return {};
        }

        int proposalCount = dims[0];
        int attributeCount = dims[1];
        bool attributesFirst = false;

        // Common YOLO RKNN export layout is [1, attributes, proposals], e.g. [1, 8, 8400].
        if (dims[0] > 0 && dims[0] <= 32 && dims[1] > dims[0]) {
            attributesFirst = true;
            attributeCount = dims[0];
            proposalCount = dims[1];
        }

        if (proposalCount <= 0 || attributeCount < 6 ||
            count < static_cast<std::size_t>(proposalCount * attributeCount)) {
            return {};
        }

        DetectionBoxes boxes;
        boxes.reserve(static_cast<std::size_t>(proposalCount));
        const auto valueAt = [data, attributeCount, proposalCount, attributesFirst](int proposal,
                                                                                     int attribute) -> float {
            if (attributesFirst) {
                return data[static_cast<std::size_t>(attribute * proposalCount + proposal)];
            }
            return data[static_cast<std::size_t>(proposal * attributeCount + attribute)];
        };

        // Heuristic:
        // - attrs-first layout on RKNN is typically YOLOv8 style: [cx, cy, w, h, cls...]
        // - proposals-first layout often comes from [x1, y1, x2, y2, obj, cls...] or similar.
        const bool hasObjectness = !attributesFirst && attributeCount > 6;

        for (int proposal = 0; proposal < proposalCount; ++proposal) {
            if (attributeCount == 6 && !attributesFirst) {
                const float x1 = valueAt(proposal, 0);
                const float y1 = valueAt(proposal, 1);
                const float x2 = valueAt(proposal, 2);
                const float y2 = valueAt(proposal, 3);
                const float score = valueAt(proposal, 4);
                const int classId =
                    std::max(0, static_cast<int>(std::round(valueAt(proposal, 5))));

                if (score < config.confidenceThreshold || x2 <= x1 || y2 <= y1) {
                    continue;
                }
                boxes.push_back(scaleBoxBack(x1, y1, x2, y2, info, score, classId));
                continue;
            }

            const float cx = valueAt(proposal, 0);
            const float cy = valueAt(proposal, 1);
            const float width = valueAt(proposal, 2);
            const float height = valueAt(proposal, 3);

            int classOffset = 4;
            float objectness = 1.0F;
            if (hasObjectness) {
                objectness = std::clamp(valueAt(proposal, 4), 0.0F, 1.0F);
                classOffset = 5;
            }

            float bestClassScore = 0.0F;
            int bestClassId = 0;
            for (int col = classOffset; col < attributeCount; ++col) {
                const float classScore = valueAt(proposal, col);
                if (classScore > bestClassScore) {
                    bestClassScore = classScore;
                    bestClassId = col - classOffset;
                }
            }

            const float score = hasObjectness ? objectness * bestClassScore : bestClassScore;
            if (score < config.confidenceThreshold) {
                continue;
            }

            const float x1 = cx - (width * 0.5F);
            const float y1 = cy - (height * 0.5F);
            const float x2 = cx + (width * 0.5F);
            const float y2 = cy + (height * 0.5F);
            if (x2 <= x1 || y2 <= y1) {
                continue;
            }
            boxes.push_back(scaleBoxBack(x1, y1, x2, y2, info, score, bestClassId));
        }

        return nonMaximumSuppression(boxes, config.nmsThreshold, config.maxDetections);
    }

    DetectionBoxes infer(const cv::Mat& bgrFrame, const Config& config) {
        if (!ready || bgrFrame.empty()) {
            return {};
        }

        const auto [inputWidth, inputHeight] = inputSize();
        if (inputWidth <= 0 || inputHeight <= 0) {
            lastError = "invalid RKNN input size";
            return {};
        }

        LetterboxInfo info;
        cv::Mat inputImage = makeLetterboxedBgr(bgrFrame, inputWidth, inputHeight, info);

        rknn_input input{};
        input.index = 0;
        input.buf = inputImage.data;
        input.size = static_cast<uint32_t>(inputImage.total() * inputImage.elemSize());
        input.pass_through = 0;
        input.type = RKNN_TENSOR_UINT8;
        input.fmt = RKNN_TENSOR_NHWC;

        if (rknn_inputs_set(context, 1, &input) < 0) {
            lastError = "rknn_inputs_set failed";
            return {};
        }
        if (rknn_run(context, nullptr) < 0) {
            lastError = "rknn_run failed";
            return {};
        }

        std::vector<rknn_output> outputs(outputAttrs.size());
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            outputs[i].want_float = 1;
            outputs[i].is_prealloc = 0;
            outputs[i].index = static_cast<uint32_t>(i);
        }

        if (rknn_outputs_get(context,
                             static_cast<uint32_t>(outputs.size()),
                             outputs.data(),
                             nullptr) < 0) {
            lastError = "rknn_outputs_get failed";
            return {};
        }

        DetectionBoxes boxes;
        if (outputs.size() == 1U && outputs[0].buf != nullptr) {
            boxes = decodeSingleOutput(
                static_cast<const float*>(outputs[0].buf),
                static_cast<std::size_t>(outputAttrs[0].n_elems),
                outputAttrs[0],
                info,
                config);
        } else if (!warnedUnsupportedOutput) {
            warnedUnsupportedOutput = true;
            std::cerr << "[RKNN] Unsupported output layout. This build currently auto-decodes only "
                         "single-output detection heads."
                      << std::endl;
        }

        rknn_outputs_release(context, static_cast<uint32_t>(outputs.size()), outputs.data());
        return boxes;
    }
#else
    bool ready{false};
    std::string lastError{"RKNN support disabled at build time"};

    bool initialize(const Config&) {
        ready = false;
        return false;
    }

    void shutdown() {
        ready = false;
    }

    DetectionBoxes infer(const cv::Mat&, const Config&) {
        return {};
    }
#endif
};

RknnDetector::RknnDetector()
    : RknnDetector(Config{}) {}

RknnDetector::RknnDetector(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

RknnDetector::~RknnDetector() {
    shutdown();
}

bool RknnDetector::initialize() {
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->initialize(config_);
}

void RknnDetector::shutdown() {
    if (impl_ != nullptr) {
        impl_->shutdown();
    }
}

bool RknnDetector::isReady() const {
    return impl_ != nullptr && impl_->ready;
}

DetectionBoxes RknnDetector::infer(const cv::Mat& bgrFrame) {
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->infer(bgrFrame, config_);
}

std::string RknnDetector::lastError() const {
    if (impl_ == nullptr) {
        return "detector implementation unavailable";
    }
    return impl_->lastError;
}
