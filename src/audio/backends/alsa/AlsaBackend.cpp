#include "audio/backends/alsa/AlsaBackend.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <unistd.h>

namespace audiocompd {
namespace {

constexpr float int16Scale = 32768.0F;

} // namespace

AlsaBackend::AlsaBackend(const AlsaConfig& config)
    : config_(config),
      format_{config.sampleRate, config.channels, config.periodFrames} {
    openDevices();
}

AlsaBackend::~AlsaBackend() {
    stop();
    closeDevices();
}

std::string AlsaBackend::name() const {
    return "ALSA";
}

AudioFormat AlsaBackend::format() const noexcept {
    return format_;
}

void AlsaBackend::start(AudioProcessCallback callback) {
    if (!callback) {
        throw std::invalid_argument("ALSA requires a valid process callback");
    }
    if (processingThread_.joinable()) {
        throw std::logic_error("ALSA backend is already running");
    }

    callback_ = std::move(callback);
    failed_ = false;
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_.clear();
    }

    checkAlsa(snd_pcm_prepare(capture_), "prepare ALSA capture device");
    checkAlsa(snd_pcm_prepare(playback_), "prepare ALSA playback device");
    running_ = true;
    processingThread_ = std::thread(&AlsaBackend::processingLoop, this);
}

void AlsaBackend::stop() noexcept {
    running_ = false;
    if (capture_ != nullptr) {
        snd_pcm_drop(capture_);
    }
    if (playback_ != nullptr) {
        snd_pcm_drop(playback_);
    }
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
}

bool AlsaBackend::failed() const noexcept {
    return failed_.load();
}

std::string AlsaBackend::failureMessage() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    return failureMessage_;
}

void AlsaBackend::checkAlsa(int result, const std::string& operation) {
    if (result < 0) {
        throw std::runtime_error(operation + ": " + snd_strerror(result));
    }
}

void AlsaBackend::openDevices() {
    try {
        checkAlsa(snd_pcm_open(&capture_, config_.inputDevice.c_str(),
                               SND_PCM_STREAM_CAPTURE, 0),
                  "open ALSA capture device " + config_.inputDevice);
        configureDevice(capture_);

        checkAlsa(snd_pcm_open(&playback_, config_.outputDevice.c_str(),
                               SND_PCM_STREAM_PLAYBACK, 0),
                  "open ALSA playback device " + config_.outputDevice);
        configureDevice(playback_);
    } catch (...) {
        closeDevices();
        throw;
    }
}

void AlsaBackend::closeDevices() noexcept {
    if (capture_ != nullptr) {
        snd_pcm_close(capture_);
        capture_ = nullptr;
    }
    if (playback_ != nullptr) {
        snd_pcm_close(playback_);
        playback_ = nullptr;
    }
}

void AlsaBackend::configureDevice(snd_pcm_t* device) {
    const auto latencyFrames = config_.periodFrames * config_.periods;
    const auto latencyUs = static_cast<unsigned int>(
        (latencyFrames * 1'000'000ULL) / config_.sampleRate);

    checkAlsa(snd_pcm_set_params(device,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 static_cast<unsigned int>(config_.channels),
                                 config_.sampleRate,
                                 1,
                                 latencyUs),
              "configure ALSA PCM device");
}

void AlsaBackend::processingLoop() noexcept {
    try {
        const std::size_t sampleCount = config_.periodFrames * config_.channels;
        std::vector<std::int16_t> input(sampleCount);
        std::vector<std::int16_t> output(sampleCount);
        std::vector<std::vector<float>> planar(
            config_.channels, std::vector<float>(config_.periodFrames));
        std::vector<float*> channelPointers(config_.channels);
        for (std::size_t channel = 0; channel < config_.channels; ++channel) {
            channelPointers[channel] = planar[channel].data();
        }

        while (running_) {
            const snd_pcm_sframes_t readFrames =
                snd_pcm_readi(capture_, input.data(), config_.periodFrames);
            if (readFrames < 0) {
                if (running_ && !recover(capture_, static_cast<int>(readFrames), "ALSA capture")) {
                    return;
                }
                continue;
            }
            if (readFrames == 0) {
                continue;
            }

            const auto frames = static_cast<std::size_t>(readFrames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t channel = 0; channel < config_.channels; ++channel) {
                    const std::size_t index = frame * config_.channels + channel;
                    planar[channel][frame] = static_cast<float>(input[index]) / int16Scale;
                }
            }

            callback_(AudioBlock{channelPointers.data(), config_.channels, frames});

            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t channel = 0; channel < config_.channels; ++channel) {
                    const float sample = std::clamp(planar[channel][frame], -1.0F, 0.999969F);
                    const std::size_t index = frame * config_.channels + channel;
                    output[index] = static_cast<std::int16_t>(std::lrint(sample * int16Scale));
                }
            }

            std::size_t writtenFrames = 0;
            while (running_ && writtenFrames < frames) {
                const snd_pcm_sframes_t result = snd_pcm_writei(
                    playback_,
                    output.data() + writtenFrames * config_.channels,
                    frames - writtenFrames);
                if (result < 0) {
                    if (!recover(playback_, static_cast<int>(result), "ALSA playback")) {
                        return;
                    }
                    continue;
                }
                writtenFrames += static_cast<std::size_t>(result);
            }
        }
    } catch (const std::exception& exception) {
        reportFailure(exception.what());
    } catch (...) {
        reportFailure("Unknown ALSA processing error");
    }
}

bool AlsaBackend::recover(snd_pcm_t* device, int error, const char* operation) noexcept {
    const int result = snd_pcm_recover(device, error, 1);
    if (result >= 0) {
        return true;
    }
    reportFailure(std::string(operation) + ": " + snd_strerror(result));
    return false;
}

void AlsaBackend::reportFailure(const std::string& message) noexcept {
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_ = message;
    }
    failed_ = true;
    running_ = false;
    ::kill(::getpid(), SIGTERM);
}

} // namespace audiocompd
