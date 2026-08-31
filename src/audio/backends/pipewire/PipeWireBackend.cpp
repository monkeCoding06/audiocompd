#include "audio/backends/pipewire/PipeWireBackend.hpp"

#include <spa/param/audio/format-utils.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <unistd.h>

namespace audiocompd {
namespace {

constexpr const char* pipeWireGroup = "audiocompd.filter";

spa_audio_info_raw makeFormat(const PipeWireConfig& config) {
    spa_audio_info_raw result{};
    result.format = SPA_AUDIO_FORMAT_F32;
    result.rate = config.sampleRate;
    result.channels = static_cast<std::uint32_t>(config.channels);
    result.position[0] = config.channels == 1 ? SPA_AUDIO_CHANNEL_MONO : SPA_AUDIO_CHANNEL_FL;
    if (config.channels == 2) {
        result.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    return result;
}

std::string latencyValue(const PipeWireConfig& config) {
    return std::to_string(config.quantum) + "/" + std::to_string(config.sampleRate);
}

} // namespace

PipeWireBackend::PipeWireBackend(const PipeWireConfig& config)
    : config_(config),
      format_{config.sampleRate, config.channels, config.quantum} {
    if (config_.channels == 0 || config_.channels > 2) {
        throw std::invalid_argument("The PipeWire backend currently supports one or two channels");
    }
    if (config_.sampleRate == 0 || config_.quantum == 0) {
        throw std::invalid_argument("PipeWire sample rate and quantum must be greater than zero");
    }

    const std::size_t capacityFrames = std::max<std::size_t>(
        static_cast<std::size_t>(config_.sampleRate / 2U), config_.quantum * 8U);
    const std::size_t strideBytes = config_.channels * sizeof(float);
    const std::size_t storageBytes = capacityFrames * strideBytes;
    if (capacityFrames > std::numeric_limits<std::uint32_t>::max() ||
        strideBytes > std::numeric_limits<std::uint32_t>::max() ||
        storageBytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("PipeWire buffer configuration is too large");
    }

    ringCapacityFrames_ = static_cast<std::uint32_t>(capacityFrames);
    frameStrideBytes_ = static_cast<std::uint32_t>(strideBytes);
    ringBufferBytes_ = static_cast<std::uint32_t>(storageBytes);
    ringStorage_.resize(capacityFrames * config_.channels);
    planarScratch_.resize(maximumBlockFrames * config_.channels);
    interleavedScratch_.resize(maximumBlockFrames * config_.channels);
    channelPointers_.resize(config_.channels);
    for (std::size_t channel = 0; channel < config_.channels; ++channel) {
        channelPointers_[channel] = planarScratch_.data() + channel * maximumBlockFrames;
    }
    spa_ringbuffer_init(&ringBuffer_);

    pw_init(nullptr, nullptr);
    loop_ = pw_thread_loop_new("audiocompd-pipewire", nullptr);
    if (loop_ == nullptr) {
        pw_deinit();
        throw std::runtime_error("Cannot create the PipeWire thread loop");
    }
}

PipeWireBackend::~PipeWireBackend() {
    stop();
    if (loop_ != nullptr) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    pw_deinit();
}

std::string PipeWireBackend::name() const {
    return "PipeWire";
}

AudioFormat PipeWireBackend::format() const noexcept {
    return format_;
}

const pw_stream_events& PipeWireBackend::captureEvents() {
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.state_changed = &PipeWireBackend::streamStateChangedCallback;
        value.process = &PipeWireBackend::captureProcessCallback;
        return value;
    }();
    return events;
}

const pw_stream_events& PipeWireBackend::playbackEvents() {
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.state_changed = &PipeWireBackend::streamStateChangedCallback;
        value.process = &PipeWireBackend::playbackProcessCallback;
        return value;
    }();
    return events;
}

void PipeWireBackend::start(AudioProcessCallback callback) {
    if (!callback) {
        throw std::invalid_argument("PipeWire requires a valid process callback");
    }
    if (active_) {
        throw std::logic_error("PipeWire backend is already running");
    }

    callback_ = std::move(callback);
    failed_ = false;
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_.clear();
    }
    spa_ringbuffer_init(&ringBuffer_);

    pw_thread_loop_lock(loop_);
    try {
        const int startResult = pw_thread_loop_start(loop_);
        if (startResult < 0) {
            throw std::runtime_error("Cannot start the PipeWire thread loop: " +
                                     std::to_string(startResult));
        }
        createStreams();
        active_ = true;
        pw_thread_loop_unlock(loop_);
    } catch (...) {
        destroyStreams();
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        callback_ = {};
        throw;
    }
}

void PipeWireBackend::stop() noexcept {
    if (!active_.exchange(false) && captureStream_ == nullptr && playbackStream_ == nullptr) {
        return;
    }

    if (loop_ != nullptr) {
        pw_thread_loop_stop(loop_);
        pw_thread_loop_lock(loop_);
        destroyStreams();
        pw_thread_loop_unlock(loop_);
    }
    callback_ = {};
}

bool PipeWireBackend::failed() const noexcept {
    return failed_.load();
}

std::string PipeWireBackend::failureMessage() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    return failureMessage_;
}

void PipeWireBackend::captureProcessCallback(void* context) noexcept {
    static_cast<PipeWireBackend*>(context)->processCapture();
}

void PipeWireBackend::playbackProcessCallback(void* context) noexcept {
    static_cast<PipeWireBackend*>(context)->processPlayback();
}

void PipeWireBackend::streamStateChangedCallback(void* context,
                                                 pw_stream_state,
                                                 pw_stream_state current,
                                                 const char* error) noexcept {
    if (current == PW_STREAM_STATE_ERROR) {
        static_cast<PipeWireBackend*>(context)->reportFailure(
            error != nullptr ? error : "Unknown PipeWire stream error");
    }
}

void PipeWireBackend::createStreams() {
    const std::string latency = latencyValue(config_);
    const std::string channels = std::to_string(config_.channels);
    const std::string rate = std::to_string(config_.sampleRate);

    captureStream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        config_.nodeName.c_str(),
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_MEDIA_CLASS, "Audio/Sink",
            PW_KEY_NODE_NAME, config_.nodeName.c_str(),
            PW_KEY_NODE_DESCRIPTION, config_.nodeDescription.c_str(),
            "node.virtual", "true",
            "node.group", pipeWireGroup,
            "node.link-group", pipeWireGroup,
            PW_KEY_NODE_LATENCY, latency.c_str(),
            "audio.channels", channels.c_str(),
            "audio.rate", rate.c_str(),
            nullptr),
        &captureEvents(),
        this);
    if (captureStream_ == nullptr) {
        throw std::runtime_error("Cannot create the PipeWire virtual sink");
    }

    std::array<std::uint8_t, 1024> capturePodStorage{};
    spa_pod_builder captureBuilder =
        SPA_POD_BUILDER_INIT(capturePodStorage.data(),
                             static_cast<std::uint32_t>(capturePodStorage.size()));
    spa_audio_info_raw captureFormat = makeFormat(config_);
    const spa_pod* captureParams[]{
        spa_format_audio_raw_build(&captureBuilder, SPA_PARAM_EnumFormat, &captureFormat)};
    const auto captureFlags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
    const int captureResult = pw_stream_connect(captureStream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                                                captureFlags, captureParams, 1);
    if (captureResult < 0) {
        throw std::runtime_error("Cannot connect the PipeWire virtual sink: " +
                                 std::to_string(captureResult));
    }

    const std::string playbackName = config_.nodeName + ".output";
    playbackStream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        playbackName.c_str(),
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_NODE_NAME, playbackName.c_str(),
            PW_KEY_NODE_DESCRIPTION, "audiocompd processed output",
            "node.passive", "true",
            "node.group", pipeWireGroup,
            "node.link-group", pipeWireGroup,
            PW_KEY_NODE_LATENCY, latency.c_str(),
            PW_KEY_TARGET_OBJECT, config_.targetSink.c_str(),
            "node.dont-fallback", "true",
            "audio.channels", channels.c_str(),
            "audio.rate", rate.c_str(),
            nullptr),
        &playbackEvents(),
        this);
    if (playbackStream_ == nullptr) {
        throw std::runtime_error("Cannot create the PipeWire playback stream");
    }

    std::array<std::uint8_t, 1024> playbackPodStorage{};
    spa_pod_builder playbackBuilder =
        SPA_POD_BUILDER_INIT(playbackPodStorage.data(),
                             static_cast<std::uint32_t>(playbackPodStorage.size()));
    spa_audio_info_raw playbackFormat = makeFormat(config_);
    const spa_pod* playbackParams[]{
        spa_format_audio_raw_build(&playbackBuilder, SPA_PARAM_EnumFormat, &playbackFormat)};
    const auto playbackFlags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
    const int playbackResult = pw_stream_connect(playbackStream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                                 playbackFlags, playbackParams, 1);
    if (playbackResult < 0) {
        throw std::runtime_error("Cannot connect to the configured PipeWire output " +
                                 config_.targetSink + ": " +
                                 std::to_string(playbackResult));
    }
}

void PipeWireBackend::destroyStreams() noexcept {
    if (playbackStream_ != nullptr) {
        pw_stream_destroy(playbackStream_);
        playbackStream_ = nullptr;
    }
    if (captureStream_ != nullptr) {
        pw_stream_destroy(captureStream_);
        captureStream_ = nullptr;
    }
}

void PipeWireBackend::processCapture() noexcept {
    pw_buffer* buffer = pw_stream_dequeue_buffer(captureStream_);
    if (buffer == nullptr) {
        return;
    }

    spa_buffer* spaBuffer = buffer->buffer;
    if (spaBuffer != nullptr && spaBuffer->n_datas > 0) {
        spa_data& data = spaBuffer->datas[0];
        if (data.data != nullptr && data.chunk != nullptr &&
            data.chunk->offset <= data.maxsize &&
            data.chunk->size <= data.maxsize - data.chunk->offset) {
            const auto* bytes = static_cast<const std::uint8_t*>(data.data);
            const auto* samples =
                reinterpret_cast<const float*>(bytes + data.chunk->offset);
            const std::size_t frames = data.chunk->size / frameStrideBytes_;
            processInterleaved(samples, frames);
        }
    }

    pw_stream_queue_buffer(captureStream_, buffer);
}

void PipeWireBackend::processPlayback() noexcept {
    pw_buffer* buffer = pw_stream_dequeue_buffer(playbackStream_);
    if (buffer == nullptr) {
        return;
    }

    spa_buffer* spaBuffer = buffer->buffer;
    if (spaBuffer != nullptr && spaBuffer->n_datas > 0) {
        spa_data& data = spaBuffer->datas[0];
        if (data.data != nullptr && data.chunk != nullptr) {
            const std::size_t maximumFrames = data.maxsize / frameStrideBytes_;
            const std::size_t requestedFrames = buffer->requested > 0
                ? std::min<std::size_t>(static_cast<std::size_t>(buffer->requested), maximumFrames)
                : maximumFrames;
            auto* output = static_cast<float*>(data.data);

            std::uint32_t readIndex = 0;
            const std::int32_t available =
                spa_ringbuffer_get_read_index(&ringBuffer_, &readIndex);
            const std::size_t readableFrames = available > 0
                ? std::min<std::size_t>(static_cast<std::size_t>(available), requestedFrames)
                : 0;
            if (readableFrames > 0) {
                spa_ringbuffer_read_data(
                    &ringBuffer_, ringStorage_.data(), ringBufferBytes_,
                    (readIndex % ringCapacityFrames_) * frameStrideBytes_,
                    output, static_cast<std::uint32_t>(readableFrames) * frameStrideBytes_);
                spa_ringbuffer_read_update(
                    &ringBuffer_, readIndex + static_cast<std::uint32_t>(readableFrames));
            }
            if (readableFrames < requestedFrames) {
                std::memset(output + readableFrames * config_.channels, 0,
                            (requestedFrames - readableFrames) * frameStrideBytes_);
            }

            data.chunk->offset = 0;
            data.chunk->stride = static_cast<std::int32_t>(frameStrideBytes_);
            data.chunk->size = static_cast<std::uint32_t>(requestedFrames) * frameStrideBytes_;
        }
    }

    pw_stream_queue_buffer(playbackStream_, buffer);
}

void PipeWireBackend::processInterleaved(const float* input, std::size_t frames) noexcept {
    std::uint32_t writeIndex = 0;
    const std::int32_t filled = spa_ringbuffer_get_write_index(&ringBuffer_, &writeIndex);
    if (filled < 0 || static_cast<std::uint32_t>(filled) > ringCapacityFrames_) {
        return;
    }

    std::size_t writableFrames = std::min<std::size_t>(
        frames, ringCapacityFrames_ - static_cast<std::uint32_t>(filled));
    std::size_t inputOffset = 0;
    while (writableFrames > 0) {
        const std::size_t blockFrames = std::min(writableFrames, maximumBlockFrames);
        for (std::size_t channel = 0; channel < config_.channels; ++channel) {
            float* destination = channelPointers_[channel];
            for (std::size_t frame = 0; frame < blockFrames; ++frame) {
                destination[frame] =
                    input[(inputOffset + frame) * config_.channels + channel];
            }
        }

        callback_(AudioBlock{channelPointers_.data(), config_.channels, blockFrames});

        for (std::size_t frame = 0; frame < blockFrames; ++frame) {
            for (std::size_t channel = 0; channel < config_.channels; ++channel) {
                interleavedScratch_[frame * config_.channels + channel] =
                    channelPointers_[channel][frame];
            }
        }

        const std::uint32_t blockFrameCount = static_cast<std::uint32_t>(blockFrames);
        spa_ringbuffer_write_data(
            &ringBuffer_, ringStorage_.data(), ringBufferBytes_,
            (writeIndex % ringCapacityFrames_) * frameStrideBytes_,
            interleavedScratch_.data(), blockFrameCount * frameStrideBytes_);
        writeIndex += blockFrameCount;
        spa_ringbuffer_write_update(&ringBuffer_, writeIndex);

        inputOffset += blockFrames;
        writableFrames -= blockFrames;
    }
}

void PipeWireBackend::reportFailure(const std::string& message) noexcept {
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        failureMessage_ = message;
    }
    failed_ = true;
    active_ = false;
    ::kill(::getpid(), SIGTERM);
}

} // namespace audiocompd

