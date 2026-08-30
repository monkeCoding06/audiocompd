#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace audiocompd {

enum class LogLevel {
    Debug = 0,
    Info,
    Warning,
    Error,
    Critical
};

class Logger {
public:
    static Logger& instance();

    static LogLevel parseLevel(const std::string& value);
    void configure(LogLevel minimumLevel, const std::string& filePath = {});

    template <typename... Args>
    void log(LogLevel level, const char* file, int line, Args&&... args) {
        if (level < minimumLevel_.load()) {
            return;
        }

        std::ostringstream message;
        (message << ... << std::forward<Args>(args));
        write(level, file, line, message.str());
    }

private:
    Logger() = default;

    void write(LogLevel level, const char* file, int line, const std::string& message);
    static const char* levelName(LogLevel level) noexcept;

    std::mutex mutex_;
    std::atomic<LogLevel> minimumLevel_{LogLevel::Info};
    std::ofstream file_;
};

} // namespace audiocompd

#define AUDIOCOMPD_LOG_DEBUG(...) \
    ::audiocompd::Logger::instance().log(::audiocompd::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define AUDIOCOMPD_LOG_INFO(...) \
    ::audiocompd::Logger::instance().log(::audiocompd::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define AUDIOCOMPD_LOG_WARNING(...) \
    ::audiocompd::Logger::instance().log(::audiocompd::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define AUDIOCOMPD_LOG_ERROR(...) \
    ::audiocompd::Logger::instance().log(::audiocompd::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define AUDIOCOMPD_LOG_CRITICAL(...) \
    ::audiocompd::Logger::instance().log(::audiocompd::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)
