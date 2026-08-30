#include "application/Application.hpp"
#include "config/Config.hpp"
#include "logging/Logger.hpp"
#include "runtime/SignalHandler.hpp"

#include <exception>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string configPath{"/etc/audiocompd/audiocompd.xml"};
    std::string schemaPath{"/usr/share/audiocompd/audiocompd.xsd"};
    bool validateOnly{};
};

void printHelp() {
    std::cout
        << "Usage: audiocompd [options]\n"
        << "  --config PATH       XML configuration path\n"
        << "  --schema PATH       XSD schema path\n"
        << "  --validate-config   Validate configuration and exit\n"
        << "  --version           Print version and exit\n"
        << "  --help              Show this help\n";
}

Arguments parseArguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" || argument == "--schema") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("Missing value after " + argument);
            }
            const std::string value = argv[++index];
            if (argument == "--config") {
                arguments.configPath = value;
            } else {
                arguments.schemaPath = value;
            }
        } else if (argument == "--validate-config") {
            arguments.validateOnly = true;
        } else if (argument == "--version") {
            std::cout << "audiocompd " << AUDIOCOMPD_VERSION << '\n';
            std::exit(0);
        } else if (argument == "--help") {
            printHelp();
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
    }
    return arguments;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parseArguments(argc, argv);
        const audiocompd::Config config =
            audiocompd::Config::load(arguments.configPath, arguments.schemaPath);

        audiocompd::Logger::instance().configure(
            audiocompd::Logger::parseLevel(config.values().logging.level),
            config.values().logging.filePath);

        if (arguments.validateOnly) {
            AUDIOCOMPD_LOG_INFO("Configuration is valid");
            return 0;
        }

        // Signals are blocked before the audio backend creates real-time threads.
        audiocompd::SignalHandler signalHandler;
        audiocompd::Application application(config);
        return application.run(signalHandler);
    } catch (const std::exception& exception) {
        AUDIOCOMPD_LOG_CRITICAL(exception.what());
        return 1;
    }
}
