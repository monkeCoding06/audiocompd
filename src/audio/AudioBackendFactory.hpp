#pragma once

#include "audio/AudioBackend.hpp"
#include "config/ConfigTypes.hpp"

#include <memory>

namespace audiocompd {

class AudioBackendFactory {
public:
    static std::unique_ptr<AudioBackend> create(const BackendConfig& config);
};

} // namespace audiocompd

