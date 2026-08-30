#include "audio/AudioEngine.hpp"

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
    compressor_.process(block);
}

} // namespace audiocompd

