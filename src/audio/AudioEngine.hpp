#pragma once

#include "audio/AudioBackend.hpp"
#include "compressor/Compressor.hpp"
#include "config/ConfigTypes.hpp"
#include <atomic>

namespace audiocompd {

class AudioEngine {
public:
    AudioEngine(AudioBackend& backend, const CompressorConfig& compressorConfig);

    void start();
    void stop() noexcept;
    struct Levels { float input; float output; float reductionDb; };
    Levels takeLevels() noexcept;
    void enableMeters(bool enabled) noexcept { metersEnabled_.store(enabled, std::memory_order_relaxed); }

private:
    void process(AudioBlock block) noexcept;

    AudioBackend& backend_;
    Compressor compressor_;
    std::atomic<float> inputPeak_{0.0F};
    std::atomic<float> outputPeak_{0.0F};
    std::atomic<float> reductionDb_{0.0F};
    std::atomic<bool> metersEnabled_{false};
};

} // namespace audiocompd
