#include "protocol/ElevatorFrameParser.hpp"

#include <cstdio>

// 字节流状态机，帧格式如下：
// AA | type | len | payload[len] | crc16_low | crc16_high | 55
void ElevatorFrameParser::parseByte(uint8_t byte) {
    switch (currentState) {
        case WAIT_HEADER:
            if (byte == 0xAA) {
                currentState = WAIT_TYPE;
                crcCalc = 0xFFFF;
                payloadBuffer.clear();
            }
            break;

        case WAIT_TYPE:
            currentType = byte;
            crcCalc = updateCrc16(crcCalc, byte);
            currentState = WAIT_LEN;
            break;

        case WAIT_LEN:
            payloadLength = byte;
            crcCalc = updateCrc16(crcCalc, byte);
            // 简单的防越界保护
            if (payloadLength > 64) {
                currentState = WAIT_HEADER;
            } else if (payloadLength > 0) {
                payloadBuffer.resize(payloadLength);
                payloadIndex = 0;
                currentState = WAIT_DATA;
            } else {
                currentState = WAIT_CRC_LOW; // 长度为0直接等 CRC
            }
            break;

        case WAIT_DATA:
            payloadBuffer[payloadIndex++] = byte;
            crcCalc = updateCrc16(crcCalc, byte);
            if (payloadIndex >= payloadLength) {
                currentState = WAIT_CRC_LOW;
            }
            break;

        case WAIT_CRC_LOW:
            crcLowByte = byte;
            currentState = WAIT_CRC_HIGH;
            break;

        case WAIT_CRC_HIGH: {
            const uint16_t recvCrc = static_cast<uint16_t>(crcLowByte) |
                                     (static_cast<uint16_t>(byte) << 8);
            if (recvCrc == crcCalc) {
                currentState = WAIT_TAIL;
            } else {
                std::fprintf(stderr,
                             "[Warning] CRC16 Error! Expected: %04X, Received: %04X\n",
                             crcCalc,
                             recvCrc);
                currentState = WAIT_HEADER;
            }
            break;
        }

        case WAIT_TAIL:
            if (byte == 0x55) {
                // 完美匹配一帧！
                processValidFrame();
            } else {
                std::cerr << "[Warning] Tail missing!" << std::endl;
            }
            // 无论成功与否，一帧结束，状态重置
            currentState = WAIT_HEADER; 
            break;
    }
}

void ElevatorFrameParser::processValidFrame() {
    // 解析已知帧类型并映射为业务数据。
    if (currentType == 0x01 && payloadLength >= 4) {
        ElevatorEnvData data;
        data.floor = payloadBuffer[0];
        data.temperature = payloadBuffer[1] + (payloadBuffer[2] / 10.0f);
        data.humidity = payloadBuffer[3];

        if (payloadLength >= 10) {
            data.frameSeq = static_cast<uint32_t>(payloadBuffer[4]) |
                            (static_cast<uint32_t>(payloadBuffer[5]) << 8);
            data.deviceTimestampMs = static_cast<uint64_t>(payloadBuffer[6]) |
                                     (static_cast<uint64_t>(payloadBuffer[7]) << 8) |
                                     (static_cast<uint64_t>(payloadBuffer[8]) << 16) |
                                     (static_cast<uint64_t>(payloadBuffer[9]) << 24);
        } else {
            data.frameSeq = 0;
            data.deviceTimestampMs = 0;
        }

        // 将解析结果上抛，上层决定具体消费方式。
        if (envCallback) {
            envCallback(data);
        }
    }
    // 这里可以继续扩展 0x02 报警指令等逻辑
}

uint16_t ElevatorFrameParser::updateCrc16(uint16_t current, uint8_t byte) {
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