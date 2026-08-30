#pragma once

#include "audio/AudioBackend.hpp"
#include "compressor/Compressor.hpp"
#include "config/ConfigTypes.hpp"

namespace audiocompd {

class AudioEngine {
public:
    AudioEngine(AudioBackend& backend, const CompressorConfig& compressorConfig);

    void start();
    void stop() noexcept;

private:
    void process(AudioBlock block) noexcept;

    AudioBackend& backend_;
    Compressor compressor_;
};

} // namespace audiocompd

