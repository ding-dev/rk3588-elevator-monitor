#include "simulation/ElevatorEnvSimulator.hpp"

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
uint16_t updateCrc16(uint16_t current, uint8_t byte) {
    uint16_t crc = current ^ byte;
    for (int i = 0; i < 8; ++i) {
        if ((crc & 0x0001) != 0U) {
            crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
        } else {
            crc = static_cast<uint16_t>(crc >> 1U);
        }
    }
    return crc;
}
} // namespace

ElevatorEnvSimulator::ElevatorEnvSimulator(ElevatorFrameParser& parser, std::atomic<bool>& keepRunning)
    : parser_(parser), keepRunning_(keepRunning) {}

ElevatorEnvSimulator::~ElevatorEnvSimulator() {
    stopAndJoin();
}

void ElevatorEnvSimulator::start() {
    if (worker_.joinable()) {
        return;
    }
    worker_ = std::thread(&ElevatorEnvSimulator::runLoop, this);
}

void ElevatorEnvSimulator::stopAndJoin() {
    // 共享停止标志让 runLoop 以协作方式退出。
    keepRunning_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ElevatorEnvSimulator::runLoop() {
    constexpr int kByteGapMs = 8;
    constexpr int kFrameIntervalMs = 2500;

    uint8_t currentFloor = 1;
    uint8_t tempInt = 26;
    uint8_t tempDec = 0;
    uint8_t humidity = 50;
    uint16_t frameSeq = 1;
    uint32_t deviceTimestampMs = 1000;

    while (keepRunning_.load()) {
        // 构造一帧模拟协议数据。
        std::vector<uint8_t> frame;

        frame.push_back(0xAA);
        frame.push_back(0x01);
        frame.push_back(0x0A);

        frame.push_back(currentFloor);
        frame.push_back(tempInt);
        frame.push_back(tempDec);
        frame.push_back(humidity);
        frame.push_back(static_cast<uint8_t>(frameSeq & 0xFF));
        frame.push_back(static_cast<uint8_t>((frameSeq >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(deviceTimestampMs & 0xFF));
        frame.push_back(static_cast<uint8_t>((deviceTimestampMs >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>((deviceTimestampMs >> 16) & 0xFF));
        frame.push_back(static_cast<uint8_t>((deviceTimestampMs >> 24) & 0xFF));

        uint16_t crc = 0xFFFF;
        for (size_t i = 1; i < frame.size(); ++i) {
            crc = updateCrc16(crc, frame[i]);
        }
        frame.push_back(static_cast<uint8_t>(crc & 0xFF));
        frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

        frame.push_back(0x55);

        for (uint8_t b : frame) {
            // 按字节喂入，模拟 UART 接收行为。
            parser_.parseByte(b);
            std::this_thread::sleep_for(std::chrono::milliseconds(kByteGapMs));
        }

        currentFloor++;
        if (currentFloor > 10) {
            currentFloor = 1;
        }

        tempDec += 2;
        if (tempDec > 9) {
            tempDec = 0;
            tempInt++;
            if (tempInt > 35) {
                tempInt = 20;
            }
        }

        humidity += 3;
        if (humidity > 80) {
            humidity = 40;
        }

        ++frameSeq;
        deviceTimestampMs += static_cast<uint32_t>(kFrameIntervalMs);

        // 将长等待切分为短等待，提升 Ctrl+C 响应速度。
        for (int i = 0; i < (kFrameIntervalMs / 100) && keepRunning_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
