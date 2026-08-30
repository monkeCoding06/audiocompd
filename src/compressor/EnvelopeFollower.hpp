#pragma once

namespace audiocompd {

class EnvelopeFollower {
public:
    void configure(float sampleRate, float attackMs, float releaseMs);
    void reset() noexcept;
    float process(float inputLevel) noexcept;

private:
    static float coefficient(float sampleRate, float timeMs) noexcept;

    float attackCoefficient_{};
    float releaseCoefficient_{};
    float envelope_{};
};

} // namespace audiocompd

