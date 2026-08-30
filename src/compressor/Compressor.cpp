#include "compressor/Compressor.hpp"

#include <algorithm>
#include <cmath>

namespace audiocompd {
namespace {

constexpr float minimumLevel = 1.0e-12F;

float linearToDb(float value) noexcept {
    return 20.0F * std::log10(std::max(value, minimumLevel));
}

float dbToLinear(float value) noexcept {
    return std::pow(10.0F, value / 20.0F);
}

} // namespace

Compressor::Compressor(const CompressorConfig& config, const AudioFormat& format)
    : config_(config) {
    envelope_.configure(static_cast<float>(format.sampleRate), config.attackMs, config.releaseMs);
}

void Compressor::process(AudioBlock block) noexcept {
    if (!config_.enabled || block.channels == nullptr || block.channelCount == 0) {
        return;
    }

    for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
        float peak = 0.0F;
        for (std::size_t channel = 0; channel < block.channelCount; ++channel) {
            peak = std::max(peak, std::abs(block.channels[channel][frame]));
        }

        const float levelDb = linearToDb(envelope_.process(peak));
        const float gain = dbToLinear(gainReductionDb(levelDb) + config_.makeupGainDb);

        for (std::size_t channel = 0; channel < block.channelCount; ++channel) {
            block.channels[channel][frame] *= gain;
        }
    }
}

void Compressor::reset() noexcept {
    envelope_.reset();
}

float Compressor::gainReductionDb(float levelDb) const noexcept {
    const float aboveThreshold = levelDb - config_.thresholdDb;
    const float slope = (1.0F / config_.ratio) - 1.0F;

    if (config_.kneeDb <= 0.0F) {
        return aboveThreshold > 0.0F ? slope * aboveThreshold : 0.0F;
    }

    const float halfKnee = config_.kneeDb * 0.5F;
    if (aboveThreshold <= -halfKnee) {
        return 0.0F;
    }
    if (aboveThreshold >= halfKnee) {
        return slope * aboveThreshold;
    }

    const float position = aboveThreshold + halfKnee;
    return slope * position * position / (2.0F * config_.kneeDb);
}

} // namespace audiocompd

