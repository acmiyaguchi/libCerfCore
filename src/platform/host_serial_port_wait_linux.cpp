#include "host_serial_port_wait.h"

#include <cerrno>
#include <chrono>
#include <poll.h>

namespace cerfcore {

namespace host_platform {

namespace {

SerialWaitResult WaitFor(int fd, short events, int stop_fd, uint32_t timeout_ms) {
    pollfd fds[2] = {{fd, events, 0}, {stop_fd, POLLIN, 0}};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const int ready = ::poll(fds, 2, left.count() > 0 ? static_cast<int>(left.count()) : 0);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return SerialWaitResult::kError;
        }
        if ((fds[1].revents & POLLIN) != 0) return SerialWaitResult::kStopped;
        if (fds[0].revents != 0) return SerialWaitResult::kReady;
        if (ready == 0) return SerialWaitResult::kTimeout;
    }
}

}

struct SerialPortWait::Impl {
    int port = -1;
    int stop = -1;
};

SerialPortWait::SerialPortWait() : impl_(std::make_unique<Impl>()) {}

SerialPortWait::~SerialPortWait() { Close(); }

bool SerialPortWait::Open(int port_fd, int stop_fd) {
    impl_->port = port_fd;
    impl_->stop = stop_fd;
    return true;
}

void SerialPortWait::Close() {
    impl_->port = -1;
    impl_->stop = -1;
}

SerialWaitResult SerialPortWait::WaitReadable(uint32_t timeout_ms) {
    return WaitFor(impl_->port, POLLIN, impl_->stop, timeout_ms);
}

SerialWaitResult SerialPortWait::WaitWritable(uint32_t timeout_ms) {
    return WaitFor(impl_->port, POLLOUT, impl_->stop, timeout_ms);
}

}

}
