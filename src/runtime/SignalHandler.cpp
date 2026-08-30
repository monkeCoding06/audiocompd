#include "runtime/SignalHandler.hpp"

#include <pthread.h>

#include <stdexcept>

namespace audiocompd {

SignalHandler::SignalHandler() {
    sigemptyset(&signalSet_);
    sigaddset(&signalSet_, SIGINT);
    sigaddset(&signalSet_, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signalSet_, &previousSet_) != 0) {
        throw std::runtime_error("Cannot block service termination signals");
    }
}

SignalHandler::~SignalHandler() {
    pthread_sigmask(SIG_SETMASK, &previousSet_, nullptr);
}

int SignalHandler::wait() const {
    int signal = 0;
    if (sigwait(&signalSet_, &signal) != 0) {
        throw std::runtime_error("Cannot wait for a service termination signal");
    }
    return signal;
}

} // namespace audiocompd

