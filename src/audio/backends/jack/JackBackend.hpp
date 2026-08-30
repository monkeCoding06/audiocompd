#pragma once

#include "audio/AudioBackend.hpp"
#include "config/ConfigTypes.hpp"

#include <jack/jack.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace audiocompd {

class JackBackend final : public AudioBackend {
public:
    explicit JackBackend(const JackConfig& config);
    ~JackBackend() override;

    std::string name() const override;
    AudioFormat format() const noexcept override;
    void start(AudioProcessCallback callback) override;
    void stop() noexcept override;
    bool failed() const noexcept override;
    std::string failureMessage() const override;

private:
    static int processCallback(jack_nframes_t frames, void* context) noexcept;
    static void shutdownCallback(void* context) noexcept;

    int process(jack_nframes_t frames) noexcept;
    void registerPorts();
    void autoConnectPorts();
    std::vector<std::string> discoverPorts(unsigned long flags) const;
    void connect(const std::string& source, const std::string& destination);
    void reportFailure(const std::string& message) noexcept;

    JackConfig config_;
    AudioFormat format_;
    jack_client_t* client_{};
    std::vector<jack_port_t*> inputPorts_;
    std::vector<jack_port_t*> outputPorts_;
    std::vector<float*> channelPointers_;
    AudioProcessCallback callback_;
    std::atomic<bool> active_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex failureMutex_;
    std::string failureMessage_;
};

} // namespace audiocompd

