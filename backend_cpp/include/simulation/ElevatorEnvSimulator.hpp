#ifndef SIMULATION_ELEVATOR_ENV_SIMULATOR_HPP
#define SIMULATION_ELEVATOR_ENV_SIMULATOR_HPP

#include <atomic>
#include <thread>

#include "protocol/ElevatorFrameParser.hpp"

class ElevatorEnvSimulator {
public:
    ElevatorEnvSimulator(ElevatorFrameParser& parser, std::atomic<bool>& keepRunning);
    ~ElevatorEnvSimulator();

    void start();
    void stopAndJoin();

private:
    void runLoop();

    ElevatorFrameParser& parser_;
    std::atomic<bool>& keepRunning_;
    std::thread worker_;
};

#endif // SIMULATION_ELEVATOR_ENV_SIMULATOR_HPP
