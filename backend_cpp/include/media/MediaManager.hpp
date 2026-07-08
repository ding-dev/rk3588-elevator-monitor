#ifndef MEDIA_MEDIA_MANAGER_HPP
#define MEDIA_MEDIA_MANAGER_HPP

#include <mutex>
#include <string>

#include "media/CameraStreamService.hpp"
#include "media/MediaTypes.hpp"
#include "media/VideoAdPlayer.hpp"

class MediaManager {
public:
    MediaManager();
    ~MediaManager();

    void setStatusCallback(MediaStatusCallback callback);

    bool startCamera();
    void stopCamera();
    bool restartCamera();
    bool playAdLoop(const std::string& videoPath);
    void stopAd();
    void shutdown();

private:
    void emitStatus(const MediaStatusEvent& event);
    void handleCameraEvent(const MediaStatusEvent& event);
    void handleAdEvent(const MediaStatusEvent& event);

    mutable std::mutex mutex_;
    CameraStreamService cameraStream_;
    VideoAdPlayer adPlayer_;
    MediaStatusCallback statusCallback_;
    bool cameraDesiredOn_{false};
};

#endif // MEDIA_MEDIA_MANAGER_HPP
