#include "app/ApplicationRuntime.hpp"

#include <chrono>
#include <iostream>
#include <thread>

ApplicationRuntime::ApplicationRuntime()
    // 在构造阶段绑定固定本地 Unix Socket 端点。
    : telemetry_("/tmp/elevator_telemetry.sock"), commandListener_("/tmp/elevator_cmd.sock") {
    mediaManager_.setStatusCallback([this](const MediaStatusEvent& event) {
        telemetry_.publishMediaStatus(event);
    });
}

ApplicationRuntime::~ApplicationRuntime() {
    stop();
}

bool ApplicationRuntime::start() {
    if (started_) {
        return true;
    }

    // 先清理历史残留广告进程，避免异常退出后遗留播放。
    mediaManager_.stopAd();
    gui_.start(320, 480);

    // Parser 是数据入口：同时驱动 UI 更新和 UDP telemetry 分发。
    parser_.setEnvDataCallback([this](const ElevatorEnvData& data) {
        if (data.frameSeq != 0U) {
            if (hasLastFrameSeq_ && data.frameSeq <= lastFrameSeq_) {
                return;
            }
            lastFrameSeq_ = data.frameSeq;
            hasLastFrameSeq_ = true;
        }

        gui_.postEnvData(data);
        telemetry_.publishEnvData(data);
    });

    // 注册来自云端 UDP 控制通道的命令处理函数。
    UdpCommandHandlers handlers;
    handlers.onAlarmTrigger = [this]() { gui_.triggerAlert(); };
    handlers.onAlarmCancel = [this]() { gui_.cancelAlert(); };
    handlers.onPlayAd = [this](const std::string& videoPath) {
        std::cout << "\n>>> [Video] Ready to LOOP on RK3588: " << videoPath << " <<<" << std::endl;
        if (!mediaManager_.playAdLoop(videoPath)) {
            std::cerr << "[Video] Failed to start ad loop playback." << std::endl;
        }
    };
    handlers.onStopAd = [this]() {
        std::cout << "\n>>> [Video] STOPPING AD PLAYBACK! <<<" << std::endl;
        mediaManager_.stopAd();
    };
    handlers.onStartCamera = [this]() {
        std::cout << "\n>>> [Camera] STARTING CAMERA STREAM <<<" << std::endl;
        if (!mediaManager_.startCamera()) {
            std::cerr << "[Camera] Failed to start camera stream." << std::endl;
        }
    };
    handlers.onStopCamera = [this]() {
        std::cout << "\n>>> [Camera] STOPPING CAMERA STREAM <<<" << std::endl;
        mediaManager_.stopCamera();
    };

    if (!commandListener_.start(keepRunning_, handlers)) {
        std::cerr << "[IPC] Failed to start command listener on /tmp/elevator_cmd.sock." << std::endl;
        keepRunning_ = false;
        return false;
    }

    if (!mediaManager_.startCamera()) {
        std::cerr << "[Camera] Failed to start initial camera stream." << std::endl;
    }

    std::cout << "Starting dynamic serial simulation...\n" << std::endl;
    // Simulator 在工作线程中持续向 parser 喂入字节流。
    simulator_ = std::make_unique<ElevatorEnvSimulator>(parser_, keepRunning_);
    simulator_->start();
    started_ = true;
    return true;
}

void ApplicationRuntime::runUntilStopped() {
    // 在 orchestrator 线程中保持轻量等待循环。
    while (keepRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ApplicationRuntime::requestStop() {
    keepRunning_ = false;
}

void ApplicationRuntime::stop() {
    if (!started_) {
        return;
    }

    // 停止顺序很关键：先停生产者，再关闭消费者。
    requestStop();
    if (simulator_ != nullptr) {
        simulator_->stopAndJoin();
    }

    std::cout << "Test Finished. Cleaning up..." << std::endl;
    commandListener_.stop();
    mediaManager_.shutdown();
    gui_.stop();
    started_ = false;
}
