#include "application/Application.hpp"

#include "audio/AudioBackendFactory.hpp"
#include "logging/Logger.hpp"

#include <csignal>

namespace audiocompd {

Application::Application(const Config& config)
    : backend_(AudioBackendFactory::create(config.values().backend)),
      engine_(std::make_unique<AudioEngine>(*backend_, config.values().compressor)) {}

int Application::run(SignalHandler& signalHandler) {
    const AudioFormat format = backend_->format();
    AUDIOCOMPD_LOG_INFO("Starting ", backend_->name(), " backend: ", format.sampleRate,
                       " Hz, ", format.channels, " channel(s), ",
                       format.framesPerBuffer, " frames per buffer");

    engine_->start();
    AUDIOCOMPD_LOG_INFO("audiocompd is running");

    int signal = 0;
    try {
        signal = signalHandler.wait();
    } catch (...) {
        engine_->stop();
        throw;
    }
    AUDIOCOMPD_LOG_INFO("Received signal ", signal, "; stopping audiocompd");

    engine_->stop();

    if (backend_->failed()) {
        AUDIOCOMPD_LOG_ERROR("Audio backend failed: ", backend_->failureMessage());
        return 1;
    }

    AUDIOCOMPD_LOG_INFO("audiocompd stopped cleanly");
    return 0;
}

} // namespace audiocompd
