#pragma once

#include "audio/AudioBackend.hpp"
#include "config/ConfigTypes.hpp"

#include <alsa/asoundlib.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace audiocompd {

class AlsaBackend final : public AudioBackend {
public:
    explicit AlsaBackend(const AlsaConfig& config);
    ~AlsaBackend() override;

    std::string name() const override;
    AudioFormat format() const noexcept override;
    void start(AudioProcessCallback callback) override;
    void stop() noexcept override;
    bool failed() const noexcept override;
    std::string failureMessage() const override;

private:
    static void checkAlsa(int result, const std::string& operation);
    void openDevices();
    void closeDevices() noexcept;
    void configureDevice(snd_pcm_t* device);
    void processingLoop() noexcept;
    bool recover(snd_pcm_t* device, int error, const char* operation) noexcept;
    void reportFailure(const std::string& message) noexcept;

    AlsaConfig config_;
    AudioFormat format_;
    snd_pcm_t* capture_{};
    snd_pcm_t* playback_{};
    AudioProcessCallback callback_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    std::thread processingThread_;
    mutable std::mutex failureMutex_;
    std::string failureMessage_;
};

} // namespace audiocompd

