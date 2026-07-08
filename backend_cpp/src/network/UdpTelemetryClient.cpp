#include "network/UdpTelemetryClient.hpp"

#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

const char* mediaComponentName(MediaComponent component) {
    switch (component) {
        case MediaComponent::Camera:
            return "camera";
        case MediaComponent::Advertisement:
            return "advertisement";
    }
    return "unknown";
}

const char* mediaStateName(MediaState state) {
    switch (state) {
        case MediaState::Starting:
            return "starting";
        case MediaState::Playing:
            return "playing";
        case MediaState::Stopped:
            return "stopped";
        case MediaState::Restarting:
            return "restarting";
        case MediaState::Failed:
            return "failed";
    }
    return "unknown";
}

} // namespace

UdpTelemetryClient::UdpTelemetryClient(const std::string& socketPath)
    : sockfd_(socket(AF_UNIX, SOCK_DGRAM, 0)), remoteAddr_(new sockaddr_un{}) {
    if (sockfd_ < 0) {
        return;
    }

    sockaddr_un* addr = static_cast<sockaddr_un*>(remoteAddr_);
    std::memset(addr, 0, sizeof(sockaddr_un));
    addr->sun_family = AF_UNIX;
    std::snprintf(addr->sun_path, sizeof(addr->sun_path), "%s", socketPath.c_str());
    remoteAddrLen_ = static_cast<unsigned int>(offsetof(sockaddr_un, sun_path) + std::strlen(addr->sun_path) + 1U);
}

UdpTelemetryClient::~UdpTelemetryClient() {
    if (sockfd_ >= 0) {
        close(sockfd_);
    }
    delete reinterpret_cast<sockaddr_un*>(remoteAddr_);
}

bool UdpTelemetryClient::isValid() const {
    // Socket 与目标地址都初始化成功才算可用。
    return sockfd_ >= 0 && remoteAddr_ != nullptr && remoteAddrLen_ > 0;
}

void UdpTelemetryClient::publishEnvData(const ElevatorEnvData& data) {
    if (!isValid()) {
        return;
    }

    sendJson(data.floor,
             data.temperature,
             data.humidity,
             1,
             data.frameSeq,
             data.deviceTimestampMs);
}

void UdpTelemetryClient::publishMediaStatus(const MediaStatusEvent& event) {
    if (!isValid()) {
        return;
    }

    sendMediaJson(event);
}

void UdpTelemetryClient::sendJson(int floor,
                                  float temperature,
                                  float humidity,
                                  int sampleCount,
                                  uint32_t frameSeq,
                                  uint64_t deviceTimestampMs) const {
    if (!isValid()) {
        return;
    }

    char jsonBuf[320];
    std::snprintf(jsonBuf,
                  sizeof(jsonBuf),
                  "{\"floor\": %d, \"temperature_avg\": %.2f, \"humidity_avg\": %.2f, \"sample_count\": %d, \"frame_seq\": %u, \"device_ts_ms\": %llu, \"status\": \"normal\"}",
                  floor,
                  temperature,
                  humidity,
                  sampleCount,
                  frameSeq,
                  static_cast<unsigned long long>(deviceTimestampMs));

    sendto(sockfd_,
           jsonBuf,
           std::strlen(jsonBuf),
           0,
            reinterpret_cast<const struct sockaddr*>(remoteAddr_),
            static_cast<socklen_t>(remoteAddrLen_));
}

void UdpTelemetryClient::sendMediaJson(const MediaStatusEvent& event) const {
    char jsonBuf[768];
    std::snprintf(
        jsonBuf,
        sizeof(jsonBuf),
        "{\"type\":\"media_status\",\"component\":\"%s\",\"state\":\"%s\",\"current_file\":\"%s\",\"reason\":\"%s\"}",
        mediaComponentName(event.component),
        mediaStateName(event.state),
        event.currentFile.c_str(),
        event.reason.c_str());

    sendto(sockfd_,
           jsonBuf,
           std::strlen(jsonBuf),
           0,
            reinterpret_cast<const struct sockaddr*>(remoteAddr_),
            static_cast<socklen_t>(remoteAddrLen_));
}
