#pragma once

#include "audio/AudioEngine.hpp"

#include <atomic>
#include <thread>

namespace audiocompd {

class MeterServer {
public:
    explicit MeterServer(AudioEngine& engine) : engine_(engine) {}
    ~MeterServer();
    MeterServer(const MeterServer&) = delete;
    MeterServer& operator=(const MeterServer&) = delete;

    void start();
    void stop() noexcept;

private:
    void serve() noexcept;
    AudioEngine& engine_;
    int socket_{-1};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

int runMeterClient();

} // namespace audiocompd
