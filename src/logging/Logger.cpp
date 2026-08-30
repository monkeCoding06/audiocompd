#include "logging/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace audiocompd {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

LogLevel Logger::parseLevel(const std::string& value) {
    if (value == "debug") {
        return LogLevel::Debug;
    }
    if (value == "info") {
        return LogLevel::Info;
    }
    if (value == "warning") {
        return LogLevel::Warning;
    }
    if (value == "error") {
        return LogLevel::Error;
    }
    if (value == "critical") {
        return LogLevel::Critical;
    }
    throw std::invalid_argument("Unknown log level: " + value);
}

void Logger::configure(LogLevel minimumLevel, const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    minimumLevel_.store(minimumLevel);
    file_.close();
    if (!filePath.empty()) {
        file_.open(filePath, std::ios::app);
        if (!file_) {
            throw std::runtime_error("Cannot open log file " + filePath);
        }
    }
}

void Logger::write(LogLevel level, const char* file, int line, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream output;
    output << '[' << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "] ["
           << levelName(level) << "] " << message << " --- " << file << ':' << line;

    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << output.str() << '\n';
    if (file_) {
        file_ << output.str() << '\n';
        file_.flush();
    }
}

const char* Logger::levelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Critical:
        return "CRITICAL";
    }
    return "UNKNOWN";
}

} // namespace audiocompd
