#include "media/VideoAdPlayer.hpp"

#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <gst/gst.h>

namespace {

constexpr const char* kVideoSinkDescription =
    "queue max-size-buffers=2 leaky=downstream ! "
    "videoconvert n-threads=2 ! "
    "video/x-raw,format=BGRx ! "
    "kmssink render-rectangle=<0,0,750,600> sync=true";

constexpr const char* kBlackFramePipeline =
    "videotestsrc pattern=black num-buffers=1 ! "
    "videoconvert ! "
    "video/x-raw,format=BGRx,width=750,height=600 ! "
    "kmssink render-rectangle=<0,0,750,600> sync=true";

std::once_flag gstreamerInitFlag;

const char* stateName(GstState state) {
    return gst_element_state_get_name(state);
}

bool seekToStart(GstElement* pipeline) {
    return gst_element_seek_simple(
        pipeline,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        0);
}

std::string buildFileUri(const std::string& videoPath) {
    GError* error = nullptr;
    gchar* uri = gst_filename_to_uri(videoPath.c_str(), &error);
    if (error != nullptr || uri == nullptr) {
        const std::string reason =
            error != nullptr ? error->message : "failed to convert file path to URI";
        if (error != nullptr) {
            g_error_free(error);
        }
        throw std::runtime_error(reason);
    }

    std::string result(uri);
    g_free(uri);
    return result;
}

} // namespace

struct VideoAdPlayer::Impl {
    GstElement* pipeline{nullptr};
    GstBus* bus{nullptr};
    std::thread worker;
    std::atomic<bool> stopRequested{false};
    std::string currentFile;
};

VideoAdPlayer::VideoAdPlayer() = default;

void VideoAdPlayer::initializeGStreamer() {
    std::call_once(gstreamerInitFlag, []() {
        gst_init(nullptr, nullptr);
    });
}

VideoAdPlayer::~VideoAdPlayer() {
    stop();
}

void VideoAdPlayer::setStatusCallback(MediaStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(callback);
}

void VideoAdPlayer::emitStatus(MediaState state,
                               const std::string& currentFile,
                               const std::string& reason) const {
    MediaStatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = statusCallback_;
    }

    if (callback) {
        callback(MediaStatusEvent{MediaComponent::Advertisement, state, currentFile, reason});
    }
}

bool VideoAdPlayer::playLoop(const std::string& videoPath) {
    if (!std::filesystem::exists(videoPath)) {
        emitStatus(MediaState::Failed, videoPath, "advertisement file does not exist");
        return false;
    }

    stop();
    initializeGStreamer();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        impl_ = std::make_unique<Impl>();
        impl_->currentFile = videoPath;
    }
    emitStatus(MediaState::Starting, videoPath, "advertisement pipeline bootstrapping");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        impl_->worker = std::thread([this]() {
            std::string currentFile;
            {
                std::lock_guard<std::mutex> threadLock(mutex_);
                if (impl_ == nullptr) {
                    return;
                }
                currentFile = impl_->currentFile;
            }

            std::string videoUri;
            try {
                videoUri = buildFileUri(currentFile);
            } catch (const std::exception& ex) {
                std::cerr << "[Ad][GStreamer][Error] " << ex.what() << std::endl;
                emitStatus(MediaState::Failed, currentFile, ex.what());
                return;
            }

            GstElement* pipeline = gst_element_factory_make("playbin", "ad-player");
            if (pipeline == nullptr) {
                std::cerr << "[Ad][GStreamer][Error] Failed to create playbin." << std::endl;
                emitStatus(MediaState::Failed, currentFile, "failed to create playbin");
                return;
            }

            GError* error = nullptr;
            GstElement* videoSink = gst_parse_bin_from_description(kVideoSinkDescription, true, &error);
            if (error != nullptr || videoSink == nullptr) {
                const std::string reason =
                    error != nullptr ? error->message : "failed to create advertisement video sink";
                std::cerr << "[Ad][GStreamer][Error] " << reason << std::endl;
                if (error != nullptr) {
                    g_error_free(error);
                }
                gst_object_unref(pipeline);
                emitStatus(MediaState::Failed, currentFile, reason);
                return;
            }

            GstElement* audioSink = gst_element_factory_make("fakesink", "ad-audio-sink");
            if (audioSink == nullptr) {
                gst_object_unref(videoSink);
                gst_object_unref(pipeline);
                emitStatus(MediaState::Failed, currentFile, "failed to create advertisement audio sink");
                return;
            }

            g_object_set(pipeline,
                         "uri", videoUri.c_str(),
                         "video-sink", videoSink,
                         "audio-sink", audioSink,
                         nullptr);
            gst_object_unref(videoSink);
            gst_object_unref(audioSink);

            GstBus* bus = gst_element_get_bus(pipeline);
            {
                std::lock_guard<std::mutex> threadLock(mutex_);
                if (impl_ == nullptr || impl_->stopRequested.load()) {
                    gst_object_unref(bus);
                    gst_object_unref(pipeline);
                    return;
                }
                impl_->pipeline = pipeline;
                impl_->bus = bus;
            }

            if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
                std::cerr << "[Ad][GStreamer][Error] Failed to set advertisement pipeline to PLAYING." << std::endl;
                emitStatus(MediaState::Failed, currentFile, "failed to set advertisement pipeline to PLAYING");
            } else {
                while (true) {
                    {
                        std::lock_guard<std::mutex> threadLock(mutex_);
                        if (impl_ == nullptr || impl_->stopRequested.load()) {
                            break;
                        }
                    }

                    GstMessage* message = gst_bus_timed_pop_filtered(
                        bus,
                        500 * GST_MSECOND,
                        static_cast<GstMessageType>(
                            GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));

                    if (message == nullptr) {
                        continue;
                    }

                    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED &&
                        GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
                        GstState oldState = GST_STATE_NULL;
                        GstState newState = GST_STATE_NULL;
                        GstState pendingState = GST_STATE_NULL;
                        gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
                        std::cout << "[Ad][GStreamer][State] "
                                  << stateName(oldState) << " -> " << stateName(newState);
                        if (pendingState != GST_STATE_VOID_PENDING) {
                            std::cout << " (pending: " << stateName(pendingState) << ")";
                        }
                        std::cout << std::endl;

                        if (newState == GST_STATE_PLAYING) {
                            running_.store(true);
                            emitStatus(MediaState::Playing, currentFile, "");
                        } else if (newState == GST_STATE_NULL) {
                            running_.store(false);
                        }
                    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                        GError* busError = nullptr;
                        gchar* debugInfo = nullptr;
                        gst_message_parse_error(message, &busError, &debugInfo);
                        const std::string reason =
                            busError != nullptr ? busError->message : "unknown gstreamer error";
                        std::cerr << "[Ad][GStreamer][Error] " << reason << std::endl;
                        if (debugInfo != nullptr && debugInfo[0] != '\0') {
                            std::cerr << "[Ad][GStreamer][Debug] " << debugInfo << std::endl;
                        }
                        emitStatus(MediaState::Failed, currentFile, reason);
                        if (busError != nullptr) {
                            g_error_free(busError);
                        }
                        if (debugInfo != nullptr) {
                            g_free(debugInfo);
                        }
                        gst_message_unref(message);
                        break;
                    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                        std::cout << "[Ad][GStreamer] Advertisement reached EOS, seeking back to start." << std::endl;
                        if (!seekToStart(pipeline)) {
                            std::cerr << "[Ad][GStreamer][Error] Failed to seek advertisement back to the beginning." << std::endl;
                            emitStatus(MediaState::Failed,
                                       currentFile,
                                       "failed to seek advertisement back to beginning");
                            gst_message_unref(message);
                            break;
                        }
                        gst_element_set_state(pipeline, GST_STATE_PLAYING);
                    }

                    gst_message_unref(message);
                }
            }

            gst_element_set_state(pipeline, GST_STATE_NULL);
            running_.store(false);
            {
                std::lock_guard<std::mutex> threadLock(mutex_);
                if (impl_ != nullptr) {
                    if (impl_->bus != nullptr) {
                        gst_object_unref(impl_->bus);
                        impl_->bus = nullptr;
                    }
                    if (impl_->pipeline != nullptr) {
                        gst_object_unref(impl_->pipeline);
                        impl_->pipeline = nullptr;
                    }
                }
            }
        });
    }

    return true;
}

void VideoAdPlayer::stop() {
    Impl* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (impl_ == nullptr) {
            running_.store(false);
            return;
        }

        impl_->stopRequested.store(true);
        if (impl_->pipeline != nullptr) {
            gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
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
        return;
    }

    if (localImpl->bus != nullptr) {
        gst_object_unref(localImpl->bus);
        localImpl->bus = nullptr;
    }
    if (localImpl->pipeline != nullptr) {
        gst_object_unref(localImpl->pipeline);
        localImpl->pipeline = nullptr;
    }

    running_.store(false);
    emitStatus(MediaState::Stopped, localImpl->currentFile, "advertisement playback stopped");
}

void VideoAdPlayer::clearVideoArea() const {
    initializeGStreamer();

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(kBlackFramePipeline, &error);
    if (error != nullptr) {
        std::cerr << "[Ad][GStreamer][Error] " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    if (pipeline == nullptr) {
        return;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_object_unref(pipeline);
        return;
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus,
        500 * GST_MSECOND,
        static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (message != nullptr) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* busError = nullptr;
            gchar* debugInfo = nullptr;
            gst_message_parse_error(message, &busError, &debugInfo);
            if (busError != nullptr) {
                std::cerr << "[Ad][GStreamer][Error] " << busError->message << std::endl;
                g_error_free(busError);
            }
            if (debugInfo != nullptr && debugInfo[0] != '\0') {
                std::cerr << "[Ad][GStreamer][Debug] " << debugInfo << std::endl;
                g_free(debugInfo);
            }
        }
        gst_message_unref(message);
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

bool VideoAdPlayer::isRunning() const {
    return running_.load();
}
