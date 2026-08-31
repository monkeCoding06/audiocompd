#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace audiocompd {

struct AlsaConfig {
    std::string inputDevice;
    std::string outputDevice;
    std::uint32_t sampleRate{};
    std::size_t channels{};
    std::size_t periodFrames{};
    std::size_t periods{};
};

struct JackConfig {
    std::string clientName;
    std::size_t channels{};
    bool autoConnect{};
    std::vector<std::string> inputPorts;
    std::vector<std::string> outputPorts;
};

struct PipeWireConfig {
    std::string nodeName;
    std::string nodeDescription;
    std::string targetSink;
    std::uint32_t sampleRate{};
    std::size_t channels{};
    std::size_t quantum{};
};

using BackendConfig = std::variant<AlsaConfig, JackConfig, PipeWireConfig>;

struct CompressorConfig {
    bool enabled{};
    float thresholdDb{};
    float ratio{};
    float attackMs{};
    float releaseMs{};
    float kneeDb{};
    float makeupGainDb{};
};

struct LoggingConfig {
    std::string level;
    std::string filePath;
};

struct AppConfig {
    BackendConfig backend;
    CompressorConfig compressor;
    LoggingConfig logging;
};

} // namespace audiocompd
