#include "audio/backends/jack/JackBackend.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <utility>
#include <unistd.h>

namespace audiocompd {

JackBackend::JackBackend(const JackConfig& config) : config_(config) {
    jack_status_t status{};
    client_ = jack_client_open(config_.clientName.c_str(), JackNoStartServer, &status);
    if (client_ == nullptr) {
        throw std::runtime_error("Cannot connect to the JACK server (status " +
                                 std::to_string(static_cast<unsigned int>(status)) + ")");
    }

    try {
        format_.sampleRate = jack_get_sample_rate(client_);
        format_.channels = config_.channels;
        format_.framesPerBuffer = jack_get_buffer_size(client_);
        channelPointers_.resize(config_.channels);
        registerPorts();

        if (jack_set_process_callback(client_, &JackBackend::processCallback, this) != 0) {
            throw std::runtime_error("Cannot register the JACK process callback");
        }
        jack_on_shutdown(client_, &JackBackend::shutdownCallback, this);
    } catch (...) {
        jack_client_close(client_);
        client_ = nullptr;
        throw;
    }
}

JackBackend::~JackBackend() {
    stop();
    if (client_ != nullptr) {
        jack_client_close(client_);
    }
}

std::string JackBackend::name() const {
    return "JACK";
}

AudioFormat JackBackend::format() const noexcept {
    return format_;
}

void JackBackend::start(AudioProcessCallback callback) {
    if (!callback) {
        throw std::invalid_argument("JACK requires a valid process callback");
    }
    if (active_) {
        throw std::logic_error("JACK backend is already running");
    }

    callback_ = std::move(callback);
    failed_ = false;
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_.clear();
    }

    if (jack_activate(client_) != 0) {
        throw std::runtime_error("Cannot activate the JACK client");
    }
    active_ = true;

    try {
        if (config_.autoConnect) {
            autoConnectPorts();
        }
    } catch (...) {
        stop();
        throw;
    }
}

void JackBackend::stop() noexcept {
    if (active_.exchange(false) && client_ != nullptr) {
        jack_deactivate(client_);
    }
}

bool JackBackend::failed() const noexcept {
    return failed_.load();
}

std::string JackBackend::failureMessage() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    return failureMessage_;
}

int JackBackend::processCallback(jack_nframes_t frames, void* context) noexcept {
    return static_cast<JackBackend*>(context)->process(frames);
}

void JackBackend::shutdownCallback(void* context) noexcept {
    static_cast<JackBackend*>(context)->reportFailure("JACK server shut down");
}

int JackBackend::process(jack_nframes_t frames) noexcept {
    try {
        for (std::size_t channel = 0; channel < config_.channels; ++channel) {
            const auto* input = static_cast<const float*>(
                jack_port_get_buffer(inputPorts_[channel], frames));
            auto* output = static_cast<float*>(
                jack_port_get_buffer(outputPorts_[channel], frames));
            std::copy_n(input, frames, output);
            channelPointers_[channel] = output;
        }

        callback_(AudioBlock{channelPointers_.data(), config_.channels,
                             static_cast<std::size_t>(frames)});
        return 0;
    } catch (...) {
        reportFailure("Unhandled exception in JACK process callback");
        return 1;
    }
}

void JackBackend::registerPorts() {
    inputPorts_.reserve(config_.channels);
    outputPorts_.reserve(config_.channels);

    for (std::size_t channel = 0; channel < config_.channels; ++channel) {
        const std::string suffix = std::to_string(channel + 1);
        jack_port_t* input = jack_port_register(
            client_, ("input_" + suffix).c_str(), JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput, 0);
        jack_port_t* output = jack_port_register(
            client_, ("output_" + suffix).c_str(), JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsOutput, 0);
        if (input == nullptr || output == nullptr) {
            throw std::runtime_error("Cannot register JACK ports for channel " + suffix);
        }
        inputPorts_.push_back(input);
        outputPorts_.push_back(output);
    }
}

void JackBackend::autoConnectPorts() {
    std::vector<std::string> sources = config_.inputPorts;
    std::vector<std::string> destinations = config_.outputPorts;

    if (sources.empty()) {
        sources = discoverPorts(JackPortIsPhysical | JackPortIsOutput);
    }
    if (destinations.empty()) {
        destinations = discoverPorts(JackPortIsPhysical | JackPortIsInput);
    }

    const std::size_t inputCount = std::min(sources.size(), inputPorts_.size());
    const std::size_t outputCount = std::min(destinations.size(), outputPorts_.size());
    for (std::size_t channel = 0; channel < inputCount; ++channel) {
        connect(sources[channel], jack_port_name(inputPorts_[channel]));
    }
    for (std::size_t channel = 0; channel < outputCount; ++channel) {
        connect(jack_port_name(outputPorts_[channel]), destinations[channel]);
    }
}

std::vector<std::string> JackBackend::discoverPorts(unsigned long flags) const {
    std::vector<std::string> result;
    const char** ports = jack_get_ports(client_, nullptr, JACK_DEFAULT_AUDIO_TYPE, flags);
    if (ports == nullptr) {
        return result;
    }
    for (std::size_t index = 0; ports[index] != nullptr; ++index) {
        result.emplace_back(ports[index]);
    }
    jack_free(ports);
    return result;
}

void JackBackend::connect(const std::string& source, const std::string& destination) {
    const int result = jack_connect(client_, source.c_str(), destination.c_str());
    if (result != 0 && result != EEXIST) {
        throw std::runtime_error("Cannot connect JACK port " + source + " to " + destination);
    }
}

void JackBackend::reportFailure(const std::string& message) noexcept {
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_ = message;
    }
    failed_ = true;
    active_ = false;
    ::kill(::getpid(), SIGTERM);
}

} // namespace audiocompd
