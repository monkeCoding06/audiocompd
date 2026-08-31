#include "audio/AudioBackendFactory.hpp"

#ifdef AUDIOCOMPD_WITH_ALSA
#include "audio/backends/alsa/AlsaBackend.hpp"
#endif
#ifdef AUDIOCOMPD_WITH_JACK
#include "audio/backends/jack/JackBackend.hpp"
#endif
#ifdef AUDIOCOMPD_WITH_PIPEWIRE
#include "audio/backends/pipewire/PipeWireBackend.hpp"
#endif

#include <stdexcept>
#include <type_traits>
#include <variant>

namespace audiocompd {

std::unique_ptr<AudioBackend> AudioBackendFactory::create(const BackendConfig& config) {
    return std::visit(
        [](const auto& backendConfig) -> std::unique_ptr<AudioBackend> {
            using ConfigType = std::decay_t<decltype(backendConfig)>;

            if constexpr (std::is_same_v<ConfigType, AlsaConfig>) {
#ifdef AUDIOCOMPD_WITH_ALSA
                return std::make_unique<AlsaBackend>(backendConfig);
#else
                throw std::runtime_error("The ALSA backend was not compiled into audiocompd");
#endif
            } else if constexpr (std::is_same_v<ConfigType, JackConfig>) {
#ifdef AUDIOCOMPD_WITH_JACK
                return std::make_unique<JackBackend>(backendConfig);
#else
                throw std::runtime_error("The JACK backend was not compiled into audiocompd");
#endif
            } else if constexpr (std::is_same_v<ConfigType, PipeWireConfig>) {
#ifdef AUDIOCOMPD_WITH_PIPEWIRE
                return std::make_unique<PipeWireBackend>(backendConfig);
#else
                throw std::runtime_error("The PipeWire backend was not compiled into audiocompd");
#endif
            }
        },
        config);
}

} // namespace audiocompd

