#include "TestHarness.hpp"

int main() {
    TestRunner runner;
    registerAudioEngineTests(runner);
    registerCompressorTests(runner);
    registerEnvelopeFollowerTests(runner);
    registerConfigTests(runner);
    registerLoggerTests(runner);
    return runner.run();
}

