#include "TestHarness.hpp"

#include "logging/Logger.hpp"

void registerLoggerTests(TestRunner& runner) {
    runner.add("Logger.ParsesKnownLevels", [] {
        require(audiocompd::Logger::parseLevel("debug") == audiocompd::LogLevel::Debug,
                "debug level was parsed incorrectly");
        require(audiocompd::Logger::parseLevel("critical") == audiocompd::LogLevel::Critical,
                "critical level was parsed incorrectly");
    });

    runner.add("Logger.RejectsUnknownLevel", [] {
        requireThrows(
            [] { (void)audiocompd::Logger::parseLevel("verbose"); },
            "unknown log level was accepted");
    });
}

