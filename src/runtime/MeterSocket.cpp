#include "runtime/MeterSocket.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace audiocompd {
namespace {

// Abstract Unix sockets are scoped by UID here and disappear on process exit.
sockaddr_un meterAddress() {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string name = "audiocompd-meter-" + std::to_string(getuid());
    if (name.size() >= sizeof(address.sun_path) - 1)
        throw std::runtime_error("Meter socket name is too long");
    std::memcpy(address.sun_path + 1, name.data(), name.size());
    return address;
}

socklen_t meterAddressLength() {
    const std::string name = "audiocompd-meter-" + std::to_string(getuid());
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
}

std::string meter(float db, float low, float high) {
    constexpr int width = 42;
    const int filled = static_cast<int>(std::round(width *
        std::max(0.0F, std::min(1.0F, (db - low) / (high - low)))));
    return "[" + std::string(filled, '#') + std::string(width - filled, ' ') + "]";
}

float decibels(float linear) {
    return linear > 0.000001F ? 20.0F * std::log10(linear) : -120.0F;
}

volatile std::sig_atomic_t interrupted = 0;
extern "C" void interruptViewer(int) { interrupted = 1; }

struct TerminalScreen {
    TerminalScreen() { std::cout << "\033[?1049h\033[?25l" << std::flush; }
    ~TerminalScreen() { std::cout << "\033[?25h\033[?1049l" << std::flush; }
};

} // namespace

MeterServer::~MeterServer() { stop(); }

void MeterServer::start() {
    socket_ = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_ < 0) throw std::runtime_error("Cannot create meter socket: " + std::string(std::strerror(errno)));
    const sockaddr_un address = meterAddress();
    if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), meterAddressLength()) < 0 ||
        ::listen(socket_, 4) < 0) {
        const std::string error = std::strerror(errno);
        ::close(socket_);
        socket_ = -1;
        throw std::runtime_error("Cannot listen on meter socket: " + error);
    }
    running_.store(true);
    try {
        worker_ = std::thread([this] { serve(); });
    } catch (...) {
        running_.store(false);
        ::close(socket_);
        socket_ = -1;
        throw;
    }
}

void MeterServer::stop() noexcept {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    if (socket_ >= 0) { ::close(socket_); socket_ = -1; }
}

void MeterServer::serve() noexcept {
    std::vector<int> clients;
    while (running_.load()) {
        pollfd listener{socket_, POLLIN, 0};
        const int ready = ::poll(&listener, 1, 50);
        if (ready > 0 && (listener.revents & POLLIN)) {
            for (;;) {
                const int client = ::accept4(socket_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client < 0) break;
                ucred credentials{};
                socklen_t size = sizeof(credentials);
                if (::getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 &&
                    credentials.uid == getuid()) clients.push_back(client);
                else ::close(client);
            }
        }
        if (clients.empty()) {
            engine_.takeLevels();
            engine_.enableMeters(false);
            continue;
        }
        engine_.enableMeters(true);
        const AudioEngine::Levels levels = engine_.takeLevels();
        for (auto it = clients.begin(); it != clients.end();) {
            if (::send(*it, &levels, sizeof(levels), MSG_DONTWAIT | MSG_NOSIGNAL) != sizeof(levels)) {
                ::close(*it);
                it = clients.erase(it);
            } else ++it;
        }
    }
    engine_.enableMeters(false);
    for (int client : clients) ::close(client);
}

int runMeterClient() {
    if (!isatty(STDOUT_FILENO)) throw std::runtime_error("--visualize requires a terminal on stdout");
    const int connection = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (connection < 0) throw std::runtime_error("Cannot create meter client socket");
    const sockaddr_un address = meterAddress();
    if (::connect(connection, reinterpret_cast<const sockaddr*>(&address), meterAddressLength()) < 0) {
        ::close(connection);
        throw std::runtime_error("Cannot connect to audiocompd; start the user service first");
    }

    struct sigaction action{};
    action.sa_handler = interruptViewer;
    sigemptyset(&action.sa_mask);
    struct sigaction previous{};
    sigaction(SIGINT, &action, &previous);
    struct sigaction previousTerm{};
    sigaction(SIGTERM, &action, &previousTerm);
    interrupted = 0;
    {
        TerminalScreen screen;
        while (!interrupted) {
            pollfd incoming{connection, POLLIN, 0};
            const int ready = ::poll(&incoming, 1, 200);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) break;
            if (ready == 0) continue;
            AudioEngine::Levels levels{};
            if (::recv(connection, &levels, sizeof(levels), 0) != sizeof(levels)) break;
            const float input = decibels(levels.input);
            const float output = decibels(levels.output);
            std::ostringstream display;
            display << "\033[H\033[J audiocompd  |  live daemon meters  |  Ctrl+C to quit\n\n"
                    << std::fixed << std::setprecision(1)
                    << " INPUT   " << meter(input, -60.0F, 0.0F) << " " << std::setw(6) << input << " dBFS\n"
                    << " OUTPUT  " << meter(output, -60.0F, 0.0F) << " " << std::setw(6) << output << " dBFS\n"
                    << " REDUCT  " << meter(levels.reductionDb, 0.0F, 30.0F) << " "
                    << std::setw(6) << levels.reductionDb << " dB\n\n"
                    << " Level scale: -60 to 0 dBFS | Reduction scale: 0 to 30 dB\n";
            std::cout << display.str() << std::flush;
        }
    }
    sigaction(SIGINT, &previous, nullptr);
    sigaction(SIGTERM, &previousTerm, nullptr);
    ::close(connection);
    if (!interrupted) throw std::runtime_error("Meter connection closed; audiocompd may have stopped");
    return 0;
}

} // namespace audiocompd
