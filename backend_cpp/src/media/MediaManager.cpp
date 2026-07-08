#include "media/MediaManager.hpp"

#include <utility>

MediaManager::MediaManager() {
    cameraStream_.setStatusCallback([this](const MediaStatusEvent& event) {
        handleCameraEvent(event);
    });
    adPlayer_.setStatusCallback([this](const MediaStatusEvent& event) {
        handleAdEvent(event);
    });
}

MediaManager::~MediaManager() {
    shutdown();
}

void MediaManager::setStatusCallback(MediaStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(callback);
}

bool MediaManager::startCamera() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cameraDesiredOn_ = true;
    }

    return cameraStream_.start();
}

void MediaManager::stopCamera() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cameraDesiredOn_ = false;
    }
    cameraStream_.stop();
}

bool MediaManager::restartCamera() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cameraDesiredOn_ = true;
    }
    return cameraStream_.restart();
}

bool MediaManager::playAdLoop(const std::string& videoPath) {
    cameraStream_.setDetectionEnabled(false);
    if (adPlayer_.playLoop(videoPath)) {
        return true;
    }
    cameraStream_.setDetectionEnabled(true);
    return false;
}

void MediaManager::stopAd() {
    adPlayer_.stop();
    adPlayer_.clearVideoArea();
    cameraStream_.setDetectionEnabled(true);
}

void MediaManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cameraDesiredOn_ = false;
    }
    adPlayer_.stop();
    cameraStream_.stop();
}

void MediaManager::emitStatus(const MediaStatusEvent& event) {
    MediaStatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = statusCallback_;
    }

    if (callback) {
        callback(event);
    }
}

void MediaManager::handleCameraEvent(const MediaStatusEvent& event) {
    emitStatus(event);
}

void MediaManager::handleAdEvent(const MediaStatusEvent& event) {
    if (event.state == MediaState::Stopped || event.state == MediaState::Failed) {
        cameraStream_.setDetectionEnabled(true);
    }
    emitStatus(event);
}
