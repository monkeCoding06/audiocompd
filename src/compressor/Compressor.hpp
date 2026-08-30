#pragma once

#include "audio/AudioBackend.hpp"
#include "compressor/EnvelopeFollower.hpp"
#include "config/ConfigTypes.hpp"

namespace audiocompd {

class Compressor {
public:
    Compressor(const CompressorConfig& config, const AudioFormat& format);

    void process(AudioBlock block) noexcept;
    void reset() noexcept;

private:
    float gainReductionDb(float levelDb) const noexcept;

    CompressorConfig config_;
    EnvelopeFollower envelope_;
};

} // namespace audiocompd

