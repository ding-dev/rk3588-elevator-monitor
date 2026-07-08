#include "media/CameraStreamService.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "media/RknnDetector.hpp"

namespace {

constexpr const char* kCameraSourcePipeline =
    "v4l2src device=/dev/video21 do-timestamp=true ! "
    "image/jpeg,width=1920,height=1080,framerate=30/1 ! "
    "jpegdec ! "
    "videoconvert ! "
    "video/x-raw,format=BGR,width=1920,height=1080,framerate=30/1 ! "
    "appsink name=camera_sink emit-signals=false sync=false max-buffers=1 drop=true";

constexpr const char* kCameraPublishPipeline =
    "appsrc name=camera_src is-live=true format=time do-timestamp=true block=false "
    "caps=video/x-raw,format=BGR,width=1920,height=1080,framerate=30/1 ! "
    "videoconvert ! "
    "video/x-raw,format=NV12 ! "
    "mpph264enc bps=4000000 bps-min=3000000 bps-max=5000000 "
    "gop=60 profile=high rc-mode=cbr qp-init=20 qp-min=16 qp-max=30 ! "
    "h264parse config-interval=1 ! "
    "rtspclientsink location=rtsp://192.168.100.10:8554/live protocols=tcp";

constexpr int kFrameWidth = 1920;
constexpr int kFrameHeight = 1080;
constexpr int kFrameRate = 30;
constexpr auto kSampleWait = std::chrono::milliseconds(200);
constexpr auto kInferInterval = std::chrono::milliseconds(120);
constexpr auto kInferIntervalWhenAdPlaying = std::chrono::milliseconds(1000);
constexpr const char* kModelPath = "/home/elf/elevator_app/best_quantized.rknn";

std::once_flag gstreamerInitFlag;

const char* stateName(GstState state) {
    return gst_element_state_get_name(state);
}

void logGstMessage(const char* tag, GstMessage* message) {
    if (message == nullptr) {
        return;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED) {
        GstState oldState = GST_STATE_NULL;
        GstState newState = GST_STATE_NULL;
        GstState pendingState = GST_STATE_NULL;
        gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
        std::cout << "[" << tag << "][GStreamer][State] "
                  << stateName(oldState) << " -> " << stateName(newState);
        if (pendingState != GST_STATE_VOID_PENDING) {
            std::cout << " (pending: " << stateName(pendingState) << ")";
        }
        std::cout << std::endl;
        return;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* busError = nullptr;
        gchar* debugInfo = nullptr;
        gst_message_parse_error(message, &busError, &debugInfo);
        const std::string reason = busError != nullptr ? busError->message : "unknown gstreamer error";
        std::cerr << "[" << tag << "][GStreamer][Error] " << reason << std::endl;
        if (debugInfo != nullptr && debugInfo[0] != '\0') {
            std::cerr << "[" << tag << "][GStreamer][Debug] " << debugInfo << std::endl;
        }
        if (busError != nullptr) {
            g_error_free(busError);
        }
        if (debugInfo != nullptr) {
            g_free(debugInfo);
        }
        return;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        std::cerr << "[" << tag << "][GStreamer][Warn] Unexpected EOS." << std::endl;
    }
}

bool drainBus(GstBus* bus, const char* tag, std::string* failureReason) {
    bool healthy = true;
    while (true) {
        GstMessage* message =
            gst_bus_pop_filtered(bus,
                                 static_cast<GstMessageType>(
                                     GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (message == nullptr) {
            break;
        }

        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            if (failureReason != nullptr) {
                GError* busError = nullptr;
                gchar* debugInfo = nullptr;
                gst_message_parse_error(message, &busError, &debugInfo);
                *failureReason =
                    busError != nullptr ? busError->message : "unknown gstreamer error";
                if (busError != nullptr) {
                    g_error_free(busError);
                }
                if (debugInfo != nullptr) {
                    g_free(debugInfo);
                }
            }
            healthy = false;
        } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            if (failureReason != nullptr) {
                *failureReason = "unexpected EOS";
            }
            healthy = false;
        }

        logGstMessage(tag, message);
        gst_message_unref(message);
    }
    return healthy;
}

void drawDetections(cv::Mat& frame, const DetectionBoxes& detections) {
    for (const DetectionBox& detection : detections) {
        const cv::Rect box(
            cv::Point(static_cast<int>(std::round(detection.x1)),
                      static_cast<int>(std::round(detection.y1))),
            cv::Point(static_cast<int>(std::round(detection.x2)),
                      static_cast<int>(std::round(detection.y2))));

        cv::rectangle(frame, box, cv::Scalar(0, 220, 0), 2, cv::LINE_AA);

        const std::string text =
            detection.label + " " + cv::format("%.2f", static_cast<double>(detection.score));
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(
            text, cv::FONT_HERSHEY_SIMPLEX, 0.7, 2, &baseline);
        const int textX = std::max(0, box.x);
        const int textY = std::max(textSize.height + 4, box.y - 8);
        const cv::Rect textRect(textX - 2,
                                textY - textSize.height - 4,
                                textSize.width + 8,
                                textSize.height + baseline + 8);
        cv::rectangle(frame, textRect, cv::Scalar(0, 220, 0), cv::FILLED);
        cv::putText(frame,
                    text,
                    cv::Point(textX + 2, textY),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.7,
                    cv::Scalar(12, 12, 12),
                    2,
                    cv::LINE_AA);
    }
}

} // namespace

struct CameraStreamService::Impl {
    GstElement* sourcePipeline{nullptr};
    GstElement* publishPipeline{nullptr};
    GstBus* sourceBus{nullptr};
    GstBus* publishBus{nullptr};
    GstAppSink* appSink{nullptr};
    GstAppSrc* appSrc{nullptr};
    std::thread worker;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> detectionEnabled{true};
};

CameraStreamService::CameraStreamService() = default;

CameraStreamService::~CameraStreamService() {
    stop();
}

void CameraStreamService::initializeGStreamer() {
    std::call_once(gstreamerInitFlag, []() {
        gst_init(nullptr, nullptr);
    });
}

void CameraStreamService::setStatusCallback(MediaStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(callback);
}

void CameraStreamService::emitStatus(MediaState state, const std::string& reason) const {
    MediaStatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = statusCallback_;
    }

    if (callback) {
        callback(MediaStatusEvent{MediaComponent::Camera, state, "", reason});
    }
}

bool CameraStreamService::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (impl_ != nullptr) {
            return true;
        }
        initializeGStreamer();
        impl_ = std::make_unique<Impl>();
    }

    emitStatus(MediaState::Starting, "camera source/publisher bootstrapping");
    std::lock_guard<std::mutex> lock(mutex_);
    impl_->worker = std::thread([this]() {
        RknnDetector detector({kModelPath, 0.35F, 0.45F, 64});
        if (!detector.initialize()) {
            std::cerr << "[Camera][AI] Detector disabled: " << detector.lastError() << std::endl;
        } else {
            std::cout << "[Camera][AI] Detector ready." << std::endl;
        }

        while (true) {
            {
                std::lock_guard<std::mutex> threadLock(mutex_);
                if (impl_ == nullptr || impl_->stopRequested.load()) {
                    break;
                }
            }

            GError* sourceError = nullptr;
            GstElement* sourcePipeline = gst_parse_launch(kCameraSourcePipeline, &sourceError);
            if (sourceError != nullptr || sourcePipeline == nullptr) {
                const std::string reason =
                    sourceError != nullptr ? sourceError->message : "failed to create source pipeline";
                if (sourceError != nullptr) {
                    g_error_free(sourceError);
                }
                std::cerr << "[Camera][GStreamer][Error] " << reason << std::endl;
                emitStatus(MediaState::Failed, reason);
            } else {
                GError* publishError = nullptr;
                GstElement* publishPipeline = gst_parse_launch(kCameraPublishPipeline, &publishError);
                if (publishError != nullptr || publishPipeline == nullptr) {
                    const std::string reason = publishError != nullptr
                                                   ? publishError->message
                                                   : "failed to create publish pipeline";
                    if (publishError != nullptr) {
                        g_error_free(publishError);
                    }
                    std::cerr << "[Camera][GStreamer][Error] " << reason << std::endl;
                    gst_object_unref(sourcePipeline);
                    emitStatus(MediaState::Failed, reason);
                } else {
                    GstElement* sinkElement =
                        gst_bin_get_by_name(GST_BIN(sourcePipeline), "camera_sink");
                    GstElement* srcElement =
                        gst_bin_get_by_name(GST_BIN(publishPipeline), "camera_src");

                    if (sinkElement == nullptr || srcElement == nullptr) {
                        if (sinkElement != nullptr) {
                            gst_object_unref(sinkElement);
                        }
                        if (srcElement != nullptr) {
                            gst_object_unref(srcElement);
                        }
                        gst_object_unref(sourcePipeline);
                        gst_object_unref(publishPipeline);
                        emitStatus(MediaState::Failed, "failed to locate appsink/appsrc");
                    } else {
                        GstBus* sourceBus = gst_element_get_bus(sourcePipeline);
                        GstBus* publishBus = gst_element_get_bus(publishPipeline);
                        GstAppSink* appSink = GST_APP_SINK(sinkElement);
                        GstAppSrc* appSrc = GST_APP_SRC(srcElement);
                        {
                            std::lock_guard<std::mutex> threadLock(mutex_);
                            if (impl_ == nullptr || impl_->stopRequested.load()) {
                                gst_object_unref(sourceBus);
                                gst_object_unref(publishBus);
                                gst_object_unref(sinkElement);
                                gst_object_unref(srcElement);
                                gst_object_unref(sourcePipeline);
                                gst_object_unref(publishPipeline);
                                break;
                            }

                            impl_->sourcePipeline = sourcePipeline;
                            impl_->publishPipeline = publishPipeline;
                            impl_->sourceBus = sourceBus;
                            impl_->publishBus = publishBus;
                            impl_->appSink = appSink;
                            impl_->appSrc = appSrc;
                        }

                        gst_app_sink_set_drop(appSink, true);
                        gst_app_sink_set_max_buffers(appSink, 1U);
                        gst_app_src_set_stream_type(appSrc, GST_APP_STREAM_TYPE_STREAM);

                        const GstStateChangeReturn publishResult =
                            gst_element_set_state(publishPipeline, GST_STATE_PLAYING);
                        const GstStateChangeReturn sourceResult =
                            gst_element_set_state(sourcePipeline, GST_STATE_PLAYING);
                        if (publishResult == GST_STATE_CHANGE_FAILURE ||
                            sourceResult == GST_STATE_CHANGE_FAILURE) {
                            emitStatus(MediaState::Failed, "failed to set camera pipelines to PLAYING");
                        } else {
                            running_.store(true);
                            emitStatus(MediaState::Playing,
                                       detector.isReady() ? "camera stream with RKNN overlay active"
                                                          : "camera stream active; RKNN overlay disabled");

                            DetectionBoxes latestDetections;
                            auto nextInferAt = std::chrono::steady_clock::now();
                            std::uint64_t frameIndex = 0;
                            bool restartRequested = false;

                            while (true) {
                                {
                                    std::lock_guard<std::mutex> threadLock(mutex_);
                                    if (impl_ == nullptr || impl_->stopRequested.load()) {
                                        break;
                                    }
                                }

                                std::string failureReason;
                                if (!drainBus(sourceBus, "CameraSource", &failureReason) ||
                                    !drainBus(publishBus, "CameraPublish", &failureReason)) {
                                    emitStatus(MediaState::Failed, failureReason);
                                    restartRequested = true;
                                    break;
                                }

                                GstSample* sample = gst_app_sink_try_pull_sample(
                                    appSink,
                                    static_cast<guint64>(
                                        std::chrono::duration_cast<std::chrono::nanoseconds>(kSampleWait)
                                            .count()));
                                if (sample == nullptr) {
                                    continue;
                                }

                                GstCaps* caps = gst_sample_get_caps(sample);
                                GstBuffer* buffer = gst_sample_get_buffer(sample);
                                GstStructure* structure =
                                    caps != nullptr ? gst_caps_get_structure(caps, 0) : nullptr;

                                int width = kFrameWidth;
                                int height = kFrameHeight;
                                if (structure != nullptr) {
                                    gst_structure_get_int(structure, "width", &width);
                                    gst_structure_get_int(structure, "height", &height);
                                }

                                GstMapInfo mapInfo{};
                                if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
                                    gst_sample_unref(sample);
                                    continue;
                                }

                                cv::Mat mappedFrame(height, width, CV_8UC3, mapInfo.data);
                                cv::Mat frame = mappedFrame.clone();
                                gst_buffer_unmap(buffer, &mapInfo);
                                gst_sample_unref(sample);

                                const bool detectionEnabled =
                                    impl_ != nullptr && impl_->detectionEnabled.load();
                                if (detector.isReady() &&
                                    detectionEnabled &&
                                    std::chrono::steady_clock::now() >= nextInferAt) {
                                    latestDetections = detector.infer(frame);
                                    nextInferAt = std::chrono::steady_clock::now() + kInferInterval;
                                } else if (!detectionEnabled) {
                                    latestDetections.clear();
                                    nextInferAt =
                                        std::chrono::steady_clock::now() + kInferIntervalWhenAdPlaying;
                                }

                                if (!latestDetections.empty()) {
                                    drawDetections(frame, latestDetections);
                                }

                                GstBuffer* outBuffer = gst_buffer_new_allocate(
                                    nullptr,
                                    static_cast<gsize>(frame.total() * frame.elemSize()),
                                    nullptr);
                                if (outBuffer == nullptr) {
                                    continue;
                                }

                                GstMapInfo outMapInfo{};
                                if (!gst_buffer_map(outBuffer, &outMapInfo, GST_MAP_WRITE)) {
                                    gst_buffer_unref(outBuffer);
                                    continue;
                                }
                                std::memcpy(outMapInfo.data, frame.data, outMapInfo.size);
                                gst_buffer_unmap(outBuffer, &outMapInfo);

                                const guint64 duration = gst_util_uint64_scale_int(
                                    1, GST_SECOND, kFrameRate);
                                GST_BUFFER_PTS(outBuffer) = frameIndex * duration;
                                GST_BUFFER_DTS(outBuffer) = GST_BUFFER_PTS(outBuffer);
                                GST_BUFFER_DURATION(outBuffer) = duration;
                                ++frameIndex;

                                const GstFlowReturn pushResult =
                                    gst_app_src_push_buffer(appSrc, outBuffer);
                                if (pushResult != GST_FLOW_OK) {
                                    emitStatus(MediaState::Failed, "failed to push annotated frame");
                                    restartRequested = true;
                                    break;
                                }
                            }

                            gst_app_src_end_of_stream(appSrc);
                            running_.store(false);

                            gst_element_set_state(sourcePipeline, GST_STATE_NULL);
                            gst_element_set_state(publishPipeline, GST_STATE_NULL);

                            {
                                std::lock_guard<std::mutex> threadLock(mutex_);
                                if (impl_ != nullptr) {
                                    if (impl_->appSink != nullptr) {
                                        gst_object_unref(impl_->appSink);
                                        impl_->appSink = nullptr;
                                    }
                                    if (impl_->appSrc != nullptr) {
                                        gst_object_unref(impl_->appSrc);
                                        impl_->appSrc = nullptr;
                                    }
                                    if (impl_->sourceBus != nullptr) {
                                        gst_object_unref(impl_->sourceBus);
                                        impl_->sourceBus = nullptr;
                                    }
                                    if (impl_->publishBus != nullptr) {
                                        gst_object_unref(impl_->publishBus);
                                        impl_->publishBus = nullptr;
                                    }
                                    if (impl_->sourcePipeline != nullptr) {
                                        gst_object_unref(impl_->sourcePipeline);
                                        impl_->sourcePipeline = nullptr;
                                    }
                                    if (impl_->publishPipeline != nullptr) {
                                        gst_object_unref(impl_->publishPipeline);
                                        impl_->publishPipeline = nullptr;
                                    }
                                }
                            }

                            {
                                std::lock_guard<std::mutex> threadLock(mutex_);
                                if (impl_ == nullptr || impl_->stopRequested.load()) {
                                    break;
                                }
                            }

                            if (restartRequested) {
                                std::cout << "[Camera][GStreamer] Restarting camera pipelines after failure."
                                          << std::endl;
                                emitStatus(MediaState::Restarting,
                                           "camera source/publisher restart scheduled");
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                                continue;
                            }
                        }

                        gst_element_set_state(sourcePipeline, GST_STATE_NULL);
                        gst_element_set_state(publishPipeline, GST_STATE_NULL);

                        {
                            std::lock_guard<std::mutex> threadLock(mutex_);
                            if (impl_ != nullptr) {
                                if (impl_->appSink != nullptr) {
                                    gst_object_unref(impl_->appSink);
                                    impl_->appSink = nullptr;
                                }
                                if (impl_->appSrc != nullptr) {
                                    gst_object_unref(impl_->appSrc);
                                    impl_->appSrc = nullptr;
                                }
                                if (impl_->sourceBus != nullptr) {
                                    gst_object_unref(impl_->sourceBus);
                                    impl_->sourceBus = nullptr;
                                }
                                if (impl_->publishBus != nullptr) {
                                    gst_object_unref(impl_->publishBus);
                                    impl_->publishBus = nullptr;
                                }
                                if (impl_->sourcePipeline != nullptr) {
                                    gst_object_unref(impl_->sourcePipeline);
                                    impl_->sourcePipeline = nullptr;
                                }
                                if (impl_->publishPipeline != nullptr) {
                                    gst_object_unref(impl_->publishPipeline);
                                    impl_->publishPipeline = nullptr;
                                }
                            }
                        }
                    }
                }
            }

            {
                std::lock_guard<std::mutex> threadLock(mutex_);
                if (impl_ == nullptr || impl_->stopRequested.load()) {
                    break;
                }
            }

            std::cout << "[Camera][GStreamer] Restarting camera pipeline after setup failure." << std::endl;
            emitStatus(MediaState::Restarting, "camera source/publisher restart scheduled");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        detector.shutdown();
    });

    return true;
}

void CameraStreamService::stop() {
    Impl* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (impl_ == nullptr) {
            running_.store(false);
            return;
        }

        impl_->stopRequested.store(true);
        if (impl_->sourcePipeline != nullptr) {
            gst_element_set_state(impl_->sourcePipeline, GST_STATE_NULL);
        }
        if (impl_->publishPipeline != nullptr) {
            gst_element_set_state(impl_->publishPipeline, GST_STATE_NULL);
        }
        state = impl_.get();
    }

    if (state != nullptr && state->worker.joinable()) {
        state->worker.join();
    }

    std::unique_ptr<Impl> localImpl;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localImpl = std::move(impl_);
    }

    if (localImpl == nullptr) {
        running_.store(false);
        emitStatus(MediaState::Stopped, "camera stream stopped");
        return;
    }

    if (localImpl->appSink != nullptr) {
        gst_object_unref(localImpl->appSink);
        localImpl->appSink = nullptr;
    }
    if (localImpl->appSrc != nullptr) {
        gst_object_unref(localImpl->appSrc);
        localImpl->appSrc = nullptr;
    }
    if (localImpl->sourceBus != nullptr) {
        gst_object_unref(localImpl->sourceBus);
        localImpl->sourceBus = nullptr;
    }
    if (localImpl->publishBus != nullptr) {
        gst_object_unref(localImpl->publishBus);
        localImpl->publishBus = nullptr;
    }
    if (localImpl->sourcePipeline != nullptr) {
        gst_object_unref(localImpl->sourcePipeline);
        localImpl->sourcePipeline = nullptr;
    }
    if (localImpl->publishPipeline != nullptr) {
        gst_object_unref(localImpl->publishPipeline);
        localImpl->publishPipeline = nullptr;
    }

    running_.store(false);
    emitStatus(MediaState::Stopped, "camera stream stopped");
}

bool CameraStreamService::restart() {
    stop();
    return start();
}

bool CameraStreamService::isRunning() const {
    return running_.load();
}

void CameraStreamService::setDetectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (impl_ != nullptr) {
        impl_->detectionEnabled.store(enabled);
    }
}

bool CameraStreamService::isDetectionEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_ == nullptr || impl_->detectionEnabled.load();
}
