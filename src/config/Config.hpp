#pragma once

#include "config/ConfigTypes.hpp"

#include <string>

namespace audiocompd {

class Config {
public:
    static Config load(const std::string& xmlPath, const std::string& schemaPath);

    const AppConfig& values() const noexcept;

private:
    explicit Config(AppConfig values);

    AppConfig values_;
};

} // namespace audiocompd

