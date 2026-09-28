#include "audio/AudioEngine.hpp"
#include <algorithm>
#include <cmath>

namespace {
void publishPeak(std::atomic<float>& destination, float value) noexcept {
    float previous = destination.load(std::memory_order_relaxed);
    while (previous < value && !destination.compare_exchange_weak(
               previous, value, std::memory_order_relaxed)) {}
}

float peak(audiocompd::AudioBlock block) noexcept {
    float value = 0.0F;
    if (!block.channels) return value;
    for (std::size_t channel = 0; channel < block.channelCount; ++channel) {
        if (!block.channels[channel]) continue;
        for (std::size_t frame = 0; frame < block.frameCount; ++frame)
            value = std::max(value, std::abs(block.channels[channel][frame]));
    }
    return value;
}
}

namespace audiocompd {

AudioEngine::AudioEngine(AudioBackend& backend, const CompressorConfig& compressorConfig)
    : backend_(backend), compressor_(compressorConfig, backend.format()) {}

void AudioEngine::start() {
    backend_.start([this](AudioBlock block) { process(block); });
}

void AudioEngine::stop() noexcept {
    backend_.stop();
}

void AudioEngine::process(AudioBlock block) noexcept {
    const bool meters = metersEnabled_.load(std::memory_order_relaxed);
    const float input = meters ? peak(block) : 0.0F;
    compressor_.process(block);
    if (meters) {
        publishPeak(inputPeak_, input);
        publishPeak(outputPeak_, peak(block));
        publishPeak(reductionDb_, -compressor_.lastReductionDb());
    }
}

AudioEngine::Levels AudioEngine::takeLevels() noexcept {
    return {inputPeak_.exchange(0.0F, std::memory_order_relaxed),
            outputPeak_.exchange(0.0F, std::memory_order_relaxed),
            reductionDb_.exchange(0.0F, std::memory_order_relaxed)};
}

} // namespace audiocompd
