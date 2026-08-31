#pragma once

#include "audio/AudioBackend.hpp"
#include "config/ConfigTypes.hpp"

#include <pipewire/pipewire.h>
#include <spa/utils/ringbuffer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace audiocompd {

class PipeWireBackend final : public AudioBackend {
public:
    explicit PipeWireBackend(const PipeWireConfig& config);
    ~PipeWireBackend() override;

    std::string name() const override;
    AudioFormat format() const noexcept override;
    void start(AudioProcessCallback callback) override;
    void stop() noexcept override;
    bool failed() const noexcept override;
    std::string failureMessage() const override;

private:
    static constexpr std::size_t maximumBlockFrames = 8192;

    static void captureProcessCallback(void* context) noexcept;
    static void playbackProcessCallback(void* context) noexcept;
    static void streamStateChangedCallback(void* context,
                                           pw_stream_state previous,
                                           pw_stream_state current,
                                           const char* error) noexcept;
    static const pw_stream_events& captureEvents();
    static const pw_stream_events& playbackEvents();

    void createStreams();
    void destroyStreams() noexcept;
    void processCapture() noexcept;
    void processPlayback() noexcept;
    void processInterleaved(const float* input, std::size_t frames) noexcept;
    void reportFailure(const std::string& message) noexcept;

    PipeWireConfig config_;
    AudioFormat format_;
    pw_thread_loop* loop_{};
    pw_stream* captureStream_{};
    pw_stream* playbackStream_{};
    AudioProcessCallback callback_;

    spa_ringbuffer ringBuffer_{};
    std::uint32_t ringCapacityFrames_{};
    std::uint32_t ringBufferBytes_{};
    std::uint32_t frameStrideBytes_{};
    std::vector<float> ringStorage_;
    std::vector<float> planarScratch_;
    std::vector<float> interleavedScratch_;
    std::vector<float*> channelPointers_;

    std::atomic<bool> active_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex failureMutex_;
    std::string failureMessage_;
};

} // namespace audiocompd
