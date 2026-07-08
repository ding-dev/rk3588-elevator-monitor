#ifndef MEDIA_CAMERA_STREAM_SERVICE_HPP
#define MEDIA_CAMERA_STREAM_SERVICE_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "media/MediaTypes.hpp"

class CameraStreamService {
public:
    CameraStreamService();
    ~CameraStreamService();

    void setStatusCallback(MediaStatusCallback callback);
    bool start();
    void stop();
    bool restart();
    bool isRunning() const;
    void setDetectionEnabled(bool enabled);
    bool isDetectionEnabled() const;

private:
    struct Impl;
    static void initializeGStreamer();

    void emitStatus(MediaState state, const std::string& reason = "") const;

    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    MediaStatusCallback statusCallback_;
};

#endif // MEDIA_CAMERA_STREAM_SERVICE_HPP
