#ifndef MEDIA_VIDEO_AD_PLAYER_HPP
#define MEDIA_VIDEO_AD_PLAYER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "media/MediaTypes.hpp"

class VideoAdPlayer {
public:
    VideoAdPlayer();
    ~VideoAdPlayer();

    void setStatusCallback(MediaStatusCallback callback);
    bool playLoop(const std::string& videoPath);
    void stop();
    void clearVideoArea() const;
    bool isRunning() const;

private:
    struct Impl;

    static void initializeGStreamer();
    void emitStatus(MediaState state,
                    const std::string& currentFile = "",
                    const std::string& reason = "") const;

    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    MediaStatusCallback statusCallback_;
};

#endif // MEDIA_VIDEO_AD_PLAYER_HPP
