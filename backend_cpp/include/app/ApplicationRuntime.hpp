#ifndef APP_APPLICATION_RUNTIME_HPP
#define APP_APPLICATION_RUNTIME_HPP

#include <atomic>
#include <cstdint>
#include <memory>

#include "media/MediaManager.hpp"
#include "protocol/ElevatorFrameParser.hpp"
#include "network/UdpCommandServer.hpp"
#include "network/UdpTelemetryClient.hpp"
#include "simulation/ElevatorEnvSimulator.hpp"
#include "ui/ElevatorUiController.hpp"

class ApplicationRuntime {
public:
    ApplicationRuntime();
    ~ApplicationRuntime();

    bool start();
    void runUntilStopped();
    void requestStop();
    void stop();

private:
    std::atomic<bool> keepRunning_{true};
    bool started_{false};

    ElevatorFrameParser parser_;
    ElevatorUiController gui_;
    MediaManager mediaManager_;
    UdpTelemetryClient telemetry_;
    UdpCommandServer commandListener_;
    std::unique_ptr<ElevatorEnvSimulator> simulator_;

    uint32_t lastFrameSeq_{0};
    bool hasLastFrameSeq_{false};
};

#endif // APP_APPLICATION_RUNTIME_HPP
