#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace audiocompd {

struct AudioFormat {
    std::uint32_t sampleRate{};
    std::size_t channels{};
    std::size_t framesPerBuffer{};
};

struct AudioBlock {
    float* const* channels{};
    std::size_t channelCount{};
    std::size_t frameCount{};
};

using AudioProcessCallback = std::function<void(AudioBlock)>;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual std::string name() const = 0;
    virtual AudioFormat format() const noexcept = 0;
    virtual void start(AudioProcessCallback callback) = 0;
    virtual void stop() noexcept = 0;
    virtual bool failed() const noexcept = 0;
    virtual std::string failureMessage() const = 0;
};

} // namespace audiocompd
