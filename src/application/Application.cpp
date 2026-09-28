#include "application/Application.hpp"

#include "audio/AudioBackendFactory.hpp"
#include "logging/Logger.hpp"

#include <csignal>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
std::string meter(float db, float low, float high) {
    constexpr int width = 42;
    const int filled = static_cast<int>(std::round(width *
        std::max(0.0F, std::min(1.0F, (db - low) / (high - low)))));
    return "[" + std::string(filled, '#') + std::string(width - filled, ' ') + "]";
}

float decibels(float linear) {
    return linear > 0.000001F ? 20.0F * std::log10(linear) : -120.0F;
}
}

namespace audiocompd {

Application::Application(const Config& config)
    : backend_(AudioBackendFactory::create(config.values().backend)),
      engine_(std::make_unique<AudioEngine>(*backend_, config.values().compressor)) {}

int Application::run(SignalHandler& signalHandler, bool visualize) {
    if (visualize && !isatty(STDOUT_FILENO)) {
        throw std::runtime_error("--visualize requires a terminal on stdout");
    }
    const AudioFormat format = backend_->format();
    AUDIOCOMPD_LOG_INFO("Starting ", backend_->name(), " backend: ", format.sampleRate,
                       " Hz, ", format.channels, " channel(s), ",
                       format.framesPerBuffer, " frames per buffer");

    if (visualize) engine_->enableMeters();
    engine_->start();
    AUDIOCOMPD_LOG_INFO("audiocompd is running");

    std::atomic<bool> drawing{visualize};
    std::thread display;
    if (visualize) {
        std::cout << "\033[?1049h\033[?25l" << std::flush;
        try {
            display = std::thread([this, &drawing] {
                while (drawing.load()) {
                    const auto levels = engine_->takeLevels();
                    const float in = decibels(levels.input);
                    const float out = decibels(levels.output);
                    std::ostringstream screen;
                    screen << "\033[H\033[J"
                           << " audiocompd  |  " << backend_->name() << "  |  Ctrl+C to stop\n\n"
                           << std::fixed << std::setprecision(1)
                           << " INPUT   " << meter(in, -60.0F, 0.0F) << " " << std::setw(6) << in << " dBFS\n"
                           << " OUTPUT  " << meter(out, -60.0F, 0.0F) << " " << std::setw(6) << out << " dBFS\n"
                           << " REDUCT  " << meter(levels.reductionDb, 0.0F, 30.0F)
                           << " " << std::setw(6) << levels.reductionDb << " dB\n\n"
                           << " Level scale: -60 to 0 dBFS | Reduction scale: 0 to 30 dB\n";
                    std::cout << screen.str() << std::flush;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
        } catch (...) {
            std::cout << "\033[?25h\033[?1049l" << std::flush;
            engine_->stop();
            throw;
        }
    }

    int signal = 0;
    try {
        signal = signalHandler.wait();
    } catch (...) {
        drawing.store(false);
        if (display.joinable()) display.join();
        if (visualize) std::cout << "\033[?25h\033[?1049l" << std::flush;
        engine_->stop();
        throw;
    }
    drawing.store(false);
    if (display.joinable()) display.join();
    if (visualize) std::cout << "\033[?25h\033[?1049l" << std::flush;
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
