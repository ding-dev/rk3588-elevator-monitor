#ifndef PROTOCOL_ELEVATOR_FRAME_PARSER_HPP
#define PROTOCOL_ELEVATOR_FRAME_PARSER_HPP

#include <cstdint>
#include <vector>
#include <functional>
#include <iostream>

// 提取出的电梯环境数据结构体
struct ElevatorEnvData {
    int floor;
    float temperature;
    float humidity;
    uint32_t frameSeq;
    uint64_t deviceTimestampMs;
};

class ElevatorFrameParser {
public:
    // 定义解析成功后的回调函数类型
    using DataCallback = std::function<void(const ElevatorEnvData&)>;

        ElevatorFrameParser()
                : currentState(WAIT_HEADER),
                    currentType(0),
                    payloadLength(0),
                    payloadIndex(0),
                    crcCalc(0xFFFF),
                    crcLowByte(0) {}

    // 设置回调函数，当解析到完整一帧时触发
    void setEnvDataCallback(DataCallback cb) { envCallback = cb; }

    // 核心函数：外部源源不断地把收到的字节喂给这个函数
    void parseByte(uint8_t byte);

private:
    // 状态机枚举
    enum ParseState {
        WAIT_HEADER,
        WAIT_TYPE,
        WAIT_LEN,
        WAIT_DATA,
        WAIT_CRC_LOW,
        WAIT_CRC_HIGH,
        WAIT_TAIL
    };

    ParseState currentState;
    
    uint8_t currentType;
    uint8_t payloadLength;
    uint8_t payloadIndex;
    uint16_t crcCalc;
    uint8_t crcLowByte;
    
    std::vector<uint8_t> payloadBuffer;
    DataCallback envCallback;

    // 解析有效的数据包
    void processValidFrame();
    static uint16_t updateCrc16(uint16_t current, uint8_t byte);
};

#endif // PROTOCOL_ELEVATOR_FRAME_PARSER_HPP