#ifndef NETWORK_UDP_TELEMETRY_CLIENT_HPP
#define NETWORK_UDP_TELEMETRY_CLIENT_HPP

#include <cstdint>

#include <string>

#include "media/MediaTypes.hpp"
#include "protocol/ElevatorFrameParser.hpp"

class UdpTelemetryClient {
public:
    explicit UdpTelemetryClient(const std::string& socketPath = "/tmp/elevator_telemetry.sock");
    ~UdpTelemetryClient();

    bool isValid() const;
    void publishEnvData(const ElevatorEnvData& data);
    void publishMediaStatus(const MediaStatusEvent& event);

private:
    void sendJson(int floor,
                  float temperature,
                  float humidity,
                  int sampleCount,
                  uint32_t frameSeq,
                  uint64_t deviceTimestampMs) const;
    void sendMediaJson(const MediaStatusEvent& event) const;

    int sockfd_{-1};
    void* remoteAddr_{nullptr};
    unsigned int remoteAddrLen_{0};
};

#endif // NETWORK_UDP_TELEMETRY_CLIENT_HPP
