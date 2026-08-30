#include "compressor/EnvelopeFollower.hpp"

#include <algorithm>
#include <cmath>

namespace audiocompd {

void EnvelopeFollower::configure(float sampleRate, float attackMs, float releaseMs) {
    attackCoefficient_ = coefficient(sampleRate, attackMs);
    releaseCoefficient_ = coefficient(sampleRate, releaseMs);
    reset();
}

void EnvelopeFollower::reset() noexcept {
    envelope_ = 0.0F;
}

float EnvelopeFollower::process(float inputLevel) noexcept {
    inputLevel = std::max(0.0F, inputLevel);
    const float selected = inputLevel > envelope_ ? attackCoefficient_ : releaseCoefficient_;
    envelope_ = selected * envelope_ + (1.0F - selected) * inputLevel;
    return envelope_;
}

float EnvelopeFollower::coefficient(float sampleRate, float timeMs) noexcept {
    if (sampleRate <= 0.0F || timeMs <= 0.0F) {
        return 0.0F;
    }
    return std::exp(-1.0F / (0.001F * timeMs * sampleRate));
}

} // namespace audiocompd

