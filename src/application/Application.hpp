#pragma once

#include "audio/AudioBackend.hpp"
#include "audio/AudioEngine.hpp"
#include "config/Config.hpp"
#include "runtime/SignalHandler.hpp"

#include <memory>

namespace audiocompd {

class Application {
public:
    explicit Application(const Config& config);

    int run(SignalHandler& signalHandler);

private:
    std::unique_ptr<AudioBackend> backend_;
    std::unique_ptr<AudioEngine> engine_;
};

} // namespace audiocompd

