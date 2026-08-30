#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class TestRunner {
public:
    using Test = std::function<void()>;

    void add(std::string name, Test test) {
        tests_.emplace_back(std::move(name), std::move(test));
    }

    int run() const {
        std::size_t passed = 0;
        for (const auto& [name, test] : tests_) {
            try {
                test();
                ++passed;
                std::cout << "[  PASS  ] " << name << '\n';
            } catch (const std::exception& exception) {
                std::cerr << "[  FAIL  ] " << name << ": " << exception.what() << '\n';
            } catch (...) {
                std::cerr << "[  FAIL  ] " << name << ": unknown exception\n";
            }
        }

        std::cout << passed << '/' << tests_.size() << " tests passed\n";
        return passed == tests_.size() ? 0 : 1;
    }

private:
    std::vector<std::pair<std::string, Test>> tests_;
};

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void requireNear(float actual, float expected, float tolerance,
                        const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

template <typename Callable>
void requireThrows(Callable&& callable, const std::string& message) {
    try {
        callable();
    } catch (...) {
        return;
    }
    throw std::runtime_error(message);
}

void registerAudioEngineTests(TestRunner& runner);
void registerCompressorTests(TestRunner& runner);
void registerEnvelopeFollowerTests(TestRunner& runner);
void registerConfigTests(TestRunner& runner);
void registerLoggerTests(TestRunner& runner);

