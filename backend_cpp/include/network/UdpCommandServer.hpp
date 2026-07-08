#ifndef NETWORK_UDP_COMMAND_SERVER_HPP
#define NETWORK_UDP_COMMAND_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

struct UdpCommandHandlers {
    std::function<void()> onAlarmTrigger;
    std::function<void()> onAlarmCancel;
    std::function<void(const std::string&)> onPlayAd;
    std::function<void()> onStopAd;
    std::function<void()> onStartCamera;
    std::function<void()> onStopCamera;
};

class UdpCommandServer {
public:
    explicit UdpCommandServer(const std::string& socketPath = "/tmp/elevator_cmd.sock");
    ~UdpCommandServer();

    bool start(std::atomic<bool>& keepRunning, const UdpCommandHandlers& handlers);
    void stop();

private:
    void listenLoop();

    std::string socketPath_;
    int recvSock_{-1};
    std::atomic<bool>* keepRunning_{nullptr};
    UdpCommandHandlers handlers_{};
    std::thread worker_;
};

#endif // NETWORK_UDP_COMMAND_SERVER_HPP
