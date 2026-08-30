#include "TestHarness.hpp"

#include "compressor/Compressor.hpp"

#include <array>

namespace {

audiocompd::CompressorConfig testConfig() {
    return audiocompd::CompressorConfig{
        true, -12.0F, 4.0F, 0.0F, 0.0F, 0.0F, 0.0F};
}

} // namespace

void registerCompressorTests(TestRunner& runner) {
    runner.add("Compressor.BypassLeavesSamplesUntouched", [] {
        auto config = testConfig();
        config.enabled = false;
        audiocompd::Compressor compressor(config, {48'000, 1, 2});

        std::array<float, 2> samples{0.5F, -0.5F};
        float* channels[]{samples.data()};
        compressor.process({channels, 1, samples.size()});

        requireNear(samples[0], 0.5F, 0.0001F, "bypass changed a positive sample");
        requireNear(samples[1], -0.5F, 0.0001F, "bypass changed a negative sample");
    });

    runner.add("Compressor.AppliesConfiguredRatio", [] {
        audiocompd::Compressor compressor(testConfig(), {48'000, 1, 1});
        std::array<float, 1> samples{1.0F};
        float* channels[]{samples.data()};
        compressor.process({channels, 1, 1});

        // 0 dB input, -12 dB threshold, 4:1 ratio -> -9 dB gain.
        requireNear(samples[0], 0.3548F, 0.001F, "compressor ratio is incorrect");
    });

    runner.add("Compressor.LinksStereoChannels", [] {
        audiocompd::Compressor compressor(testConfig(), {48'000, 2, 1});
        std::array<float, 1> left{1.0F};
        std::array<float, 1> right{0.5F};
        float* channels[]{left.data(), right.data()};
        compressor.process({channels, 2, 1});

        requireNear(right[0] / left[0], 0.5F, 0.0001F,
                    "stereo linking changed the channel balance");
    });
}

