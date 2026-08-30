#include "TestHarness.hpp"

#include "audio/AudioEngine.hpp"

#include <array>
#include <string>
#include <utility>

namespace {

class MockBackend final : public audiocompd::AudioBackend {
public:
    std::string name() const override { return "mock"; }
    audiocompd::AudioFormat format() const noexcept override { return {48'000, 1, 1}; }

    void start(audiocompd::AudioProcessCallback callback) override {
        std::array<float, 1> samples{1.0F};
        float* channels[]{samples.data()};
        callback({channels, 1, 1});
        processedSample = samples[0];
        started = true;
    }

    void stop() noexcept override { stopped = true; }
    bool failed() const noexcept override { return false; }
    std::string failureMessage() const override { return {}; }

    bool started{};
    bool stopped{};
    float processedSample{};
};

} // namespace

void registerAudioEngineTests(TestRunner& runner) {
    runner.add("AudioEngine.ConnectsBackendAndCompressor", [] {
        MockBackend backend;
        const audiocompd::CompressorConfig config{
            true, -12.0F, 4.0F, 0.0F, 0.0F, 0.0F, 0.0F};
        audiocompd::AudioEngine engine(backend, config);

        engine.start();
        engine.stop();

        require(backend.started, "audio backend was not started");
        require(backend.stopped, "audio backend was not stopped");
        require(backend.processedSample < 1.0F, "audio was not compressed");
    });
}

