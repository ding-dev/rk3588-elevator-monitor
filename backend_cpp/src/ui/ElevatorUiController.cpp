#include "ui/ElevatorUiController.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include "hal/hal.h"
#include "ui.h"
}

class ElevatorUiController::Impl {
public:
    enum class AlertCommand {
        Trigger,
        Cancel
    };

    std::thread guiThread;
    std::mutex dataMutex;

    std::atomic<bool> running{false};
    bool initialized{false};

    // 待处理数据由生产者线程写入队列，在 GUI 线程批量消费。
    std::deque<ElevatorEnvData> pendingDataQueue;

    // 告警命令独立高优先级队列，避免被普通数据刷新延后。
    std::deque<AlertCommand> pendingAlertQueue;
    // ----- UI 控件指针 -----
    lv_obj_t* infoPanel{nullptr};
    lv_obj_t* labelFloor{nullptr};
    lv_obj_t* labelTemp{nullptr};   
    lv_obj_t* labelAlert{nullptr};

    lv_color_t infoPanelBgColor{0};
    lv_opa_t infoPanelBgOpa{LV_OPA_COVER};
};

ElevatorUiController::~ElevatorUiController() {
    stop();
}

bool ElevatorUiController::start(int32_t width, int32_t height) {
    if (impl_ != nullptr) {
        return true;
    }

    width_ = width;
    height_ = height;

    impl_ = new Impl();
    impl_->running = true;
    impl_->guiThread = std::thread(&ElevatorUiController::guiLoop, this);
    return true;
}

void ElevatorUiController::stop() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->running = false;
    if (impl_->guiThread.joinable()) {
        impl_->guiThread.join();
    }

    delete impl_;
    impl_ = nullptr;
}

void ElevatorUiController::postEnvData(const ElevatorEnvData& data) {
    if (impl_ == nullptr) {
        return;
    }

    // 这里不能直接操作 LVGL 对象，只做线程安全的数据交接。
    std::lock_guard<std::mutex> lock(impl_->dataMutex);
    impl_->pendingDataQueue.push_back(data);
    // 限制队列上限，避免 GUI 长时间阻塞导致内存持续增长。
    if (impl_->pendingDataQueue.size() > 120) {
        impl_->pendingDataQueue.pop_front();
    }
}

void ElevatorUiController::guiLoop() {
    // LVGL 初始化和全部控件操作必须在本线程执行。
    lv_init();
    sdl_hal_init(width_, height_);

    buildUI();
    impl_->initialized = true;

    while (impl_->running) {
        applyPendingData();

        uint32_t sleepTimeMs = lv_timer_handler();
        if (sleepTimeMs == LV_NO_TIMER_READY) {
            sleepTimeMs = LV_DEF_REFR_PERIOD;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
    }
}

void ElevatorUiController::buildUI() {
    ui_init();

    impl_->infoPanel  = ui_InfoPanel;
    impl_->labelFloor = ui_LabelFloor;
    impl_->labelTemp  = ui_LabelTemp;
    impl_->labelAlert = ui_LabelAlert;

    if (impl_->infoPanel != nullptr) {
        impl_->infoPanelBgColor = lv_obj_get_style_bg_color(impl_->infoPanel, LV_PART_MAIN);
        impl_->infoPanelBgOpa = lv_obj_get_style_bg_opa(impl_->infoPanel, LV_PART_MAIN);
    }
}

void ElevatorUiController::triggerAlert() {
    if (impl_ == nullptr) return;
    std::lock_guard<std::mutex> lock(impl_->dataMutex);
    impl_->pendingAlertQueue.push_back(Impl::AlertCommand::Trigger);
    if (impl_->pendingAlertQueue.size() > 16) {
        impl_->pendingAlertQueue.pop_front();
    }
}

void ElevatorUiController::cancelAlert() {
    if (impl_ == nullptr) return;
    std::lock_guard<std::mutex> lock(impl_->dataMutex);
    impl_->pendingAlertQueue.push_back(Impl::AlertCommand::Cancel);
    if (impl_->pendingAlertQueue.size() > 16) {
        impl_->pendingAlertQueue.pop_front();
    }
}

void ElevatorUiController::applyPendingData() {
    ElevatorEnvData localData{};
    bool hasData = false;
    bool hasAlertCmd = false;
    Impl::AlertCommand latestAlertCmd = Impl::AlertCommand::Cancel;
    {
        std::lock_guard<std::mutex> lock(impl_->dataMutex);
        if (!impl_->pendingAlertQueue.empty()) {
            latestAlertCmd = impl_->pendingAlertQueue.back();
            impl_->pendingAlertQueue.clear();
            hasAlertCmd = true;
        }

        if (!impl_->pendingDataQueue.empty()) {
            // 在一个 GUI 周期内只保留最新状态，避免显示积压历史帧导致回退闪烁。
            localData = impl_->pendingDataQueue.back();
            impl_->pendingDataQueue.clear();
            hasData = true;
        }
    }

    if (!impl_->initialized) return;

    // 告警命令先处理，保障实时性。
    if (hasAlertCmd && latestAlertCmd == Impl::AlertCommand::Trigger) {
        // 显示红色警报字样，并且让背景变红闪烁一下
        lv_obj_remove_flag(impl_->labelAlert, LV_OBJ_FLAG_HIDDEN);
        if (impl_->infoPanel != nullptr) {
            lv_obj_set_style_bg_color(impl_->infoPanel, lv_color_hex(0x550000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(impl_->infoPanel, LV_OPA_COVER, LV_PART_MAIN);
        }
    } else if (hasAlertCmd && latestAlertCmd == Impl::AlertCommand::Cancel) {
        // 隐藏红色文字，并把背景色改回原来的深灰色 (0x1E1E1E)
        lv_obj_add_flag(impl_->labelAlert, LV_OBJ_FLAG_HIDDEN);
        if (impl_->infoPanel != nullptr) {
            lv_obj_set_style_bg_color(impl_->infoPanel, impl_->infoPanelBgColor, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(impl_->infoPanel, impl_->infoPanelBgOpa, LV_PART_MAIN);
        }
    }

    // 更新常规数据
    if (hasData) {
        char floorBuf[32] = {0};
        char envBuf[96] = {0};
        std::snprintf(floorBuf, sizeof(floorBuf), "%d F", localData.floor);
        std::snprintf(envBuf, sizeof(envBuf), "Temp: %.1f C\nHumi: %.1f %%",
                      localData.temperature, localData.humidity);
        lv_label_set_text(impl_->labelFloor, floorBuf);
        lv_label_set_text(impl_->labelTemp, envBuf);
    }
}
