#include "network/UdpCommandServer.hpp"

#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

UdpCommandServer::UdpCommandServer(const std::string& socketPath)
    : socketPath_(socketPath) {}

UdpCommandServer::~UdpCommandServer() {
    stop();
}

bool UdpCommandServer::start(std::atomic<bool>& keepRunning, const UdpCommandHandlers& handlers) {
    if (worker_.joinable()) {
        return false;
    }

    keepRunning_ = &keepRunning;
    handlers_ = handlers;

    recvSock_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (recvSock_ < 0) {
        return false;
    }

    sockaddr_un recvAddr;
    std::memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sun_family = AF_UNIX;
    std::snprintf(recvAddr.sun_path, sizeof(recvAddr.sun_path), "%s", socketPath_.c_str());

    // 旧 socket 文件可能来自异常退出，先清理再 bind。
    unlink(socketPath_.c_str());

    if (bind(recvSock_, reinterpret_cast<sockaddr*>(&recvAddr), sizeof(recvAddr)) < 0) {
        close(recvSock_);
        recvSock_ = -1;
        return false;
    }

    // 设置超时，让循环能够周期性检查 keepRunning_。
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(recvSock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    worker_ = std::thread(&UdpCommandServer::listenLoop, this);
    return true;
}

void UdpCommandServer::stop() {
    if (recvSock_ >= 0) {
        close(recvSock_);
        recvSock_ = -1;
    }

    if (!socketPath_.empty()) {
        unlink(socketPath_.c_str());
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void UdpCommandServer::listenLoop() {
    if (keepRunning_ == nullptr) {
        return;
    }

    // 协议解析留在本层，业务动作通过回调下发。
    char buffer[256];
    while (keepRunning_->load()) {
        const int sock = recvSock_;
        if (sock < 0) {
            break;
        }

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);
        if (n <= 0) {
            continue;
        }

        buffer[n] = '\0';
        std::string msg(buffer);
        std::cout << "\n[C++ UDP Recv] " << msg << std::endl;

        if (msg.find("ALARM_TRIGGER") != std::string::npos) {
            if (handlers_.onAlarmTrigger) {
                handlers_.onAlarmTrigger();
            }
        } else if (msg.find("ALARM_CANCEL") != std::string::npos) {
            if (handlers_.onAlarmCancel) {
                handlers_.onAlarmCancel();
            }
        } else if (msg.find("PLAY_AD:") == 0) {
            if (handlers_.onPlayAd) {
                handlers_.onPlayAd(msg.substr(8));
            }
        } else if (msg.find("STOP_AD") == 0) {
            if (handlers_.onStopAd) {
                handlers_.onStopAd();
            }
        } else if (msg.find("START_CAMERA") == 0) {
            if (handlers_.onStartCamera) {
                handlers_.onStartCamera();
            }
        } else if (msg.find("STOP_CAMERA") == 0) {
            if (handlers_.onStopCamera) {
                handlers_.onStopCamera();
            }
        }
    }
}
