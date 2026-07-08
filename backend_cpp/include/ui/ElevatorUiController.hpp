#ifndef UI_ELEVATOR_UI_CONTROLLER_HPP
#define UI_ELEVATOR_UI_CONTROLLER_HPP

#include <cstdint>

#include "protocol/ElevatorFrameParser.hpp"

class ElevatorUiController {
public:
    ElevatorUiController() = default;
    ~ElevatorUiController();

    bool start(int32_t width = 320, int32_t height = 480);
    void stop();

    // 线程安全的生产者接口：serial/parser 线程可直接调用。
    void postEnvData(const ElevatorEnvData& data);
    void triggerAlert();
    void cancelAlert();
private:
    ElevatorUiController(const ElevatorUiController&) = delete;
    ElevatorUiController& operator=(const ElevatorUiController&) = delete;

    void guiLoop();
    void buildUI();
    void applyPendingData();    

    int32_t width_{320};
    int32_t height_{480};

    class Impl;
    Impl* impl_{nullptr};
};

#endif // UI_ELEVATOR_UI_CONTROLLER_HPP
