#include "TestHarness.hpp"

#include "config/Config.hpp"

#include <string>
#include <variant>

void registerConfigTests(TestRunner& runner) {
    runner.add("Config.ValidatesAndLoadsPipeWireConfiguration", [] {
        const std::string root = AUDIOCOMPD_TEST_SOURCE_DIR;
        const auto config = audiocompd::Config::load(
            root + "/config/audiocompd.xml", root + "/schema/audiocompd.xsd");

        require(std::holds_alternative<audiocompd::PipeWireConfig>(config.values().backend),
                "PipeWire backend was not selected");
        const auto& pipewire = std::get<audiocompd::PipeWireConfig>(config.values().backend);
        require(pipewire.targetSink ==
                    "alsa_output.pci-0000_0a_00.4.analog-stereo",
                "target sink was parsed incorrectly");
        require(pipewire.sampleRate == 48'000, "sample rate was parsed incorrectly");
        require(pipewire.channels == 2, "channel count was parsed incorrectly");
        require(pipewire.quantum == 256, "quantum was parsed incorrectly");
        requireNear(config.values().compressor.ratio, 4.0F, 0.0001F,
                    "compressor ratio was parsed incorrectly");
    });

    runner.add("Config.LoadsAlsaConfiguration", [] {
        const std::string root = AUDIOCOMPD_TEST_SOURCE_DIR;
        const auto config = audiocompd::Config::load(
            root + "/config/audiocompd-alsa.example.xml",
            root + "/schema/audiocompd.xsd");

        require(std::holds_alternative<audiocompd::AlsaConfig>(config.values().backend),
                "ALSA backend was not selected");
        const auto& alsa = std::get<audiocompd::AlsaConfig>(config.values().backend);
        require(alsa.sampleRate == 48'000, "ALSA sample rate was parsed incorrectly");
        require(alsa.channels == 2, "ALSA channel count was parsed incorrectly");
    });

    runner.add("Config.LoadsJackConfiguration", [] {
        const std::string root = AUDIOCOMPD_TEST_SOURCE_DIR;
        const auto config = audiocompd::Config::load(
            root + "/config/audiocompd-jack.example.xml",
            root + "/schema/audiocompd.xsd");

        require(std::holds_alternative<audiocompd::JackConfig>(config.values().backend),
                "JACK backend was not selected");
        const auto& jack = std::get<audiocompd::JackConfig>(config.values().backend);
        require(jack.clientName == "audiocompd", "JACK client name was parsed incorrectly");
        require(jack.autoConnect, "JACK auto-connect was parsed incorrectly");
    });

    runner.add("Config.RejectsMissingConfiguration", [] {
        const std::string root = AUDIOCOMPD_TEST_SOURCE_DIR;
        requireThrows(
            [&] {
                (void)audiocompd::Config::load(
                    root + "/config/does-not-exist.xml",
                    root + "/schema/audiocompd.xsd");
            },
            "missing XML configuration was accepted");
    });
}
