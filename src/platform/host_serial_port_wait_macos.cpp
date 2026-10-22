#include "host_serial_port_wait.h"

#include <cerrno>
#include <chrono>
#include <sys/event.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

timespec TimeLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
        deadline - std::chrono::steady_clock::now());
    const int64_t ns = left.count() > 0 ? static_cast<int64_t>(left.count()) : 0;
    return timespec{static_cast<time_t>(ns / 1000000000), static_cast<long>(ns % 1000000000)};
}

SerialWaitResult Wait(int queue, int port, int stop, int16_t filter, uint32_t timeout_ms) {
    struct kevent change;
    EV_SET(&change, static_cast<uintptr_t>(port), filter, EV_ADD | EV_ONESHOT, 0u, 0, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const timespec left = TimeLeft(deadline);
        struct kevent fired[2];
        const int n = ::kevent(queue, &change, 1, fired, 2, &left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return SerialWaitResult::kError;
        }
        for (int i = 0; i < n; ++i) {
            if ((fired[i].flags & EV_ERROR) != 0) {
                errno = static_cast<int>(fired[i].data);
                return SerialWaitResult::kError;
            }
        }
        for (int i = 0; i < n; ++i) {
            if (stop >= 0 && fired[i].ident == static_cast<uintptr_t>(stop)) {
                return SerialWaitResult::kStopped;
            }
        }
        if (n > 0) return SerialWaitResult::kReady;
        struct kevent drop;
        EV_SET(&drop, static_cast<uintptr_t>(port), filter, EV_DELETE, 0u, 0, nullptr);
        ::kevent(queue, &drop, 1, nullptr, 0, nullptr);
        return SerialWaitResult::kTimeout;
    }
}

}

struct SerialPortWait::Impl {
    int port  = -1;
    int stop  = -1;
    int queue = -1;
};

SerialPortWait::SerialPortWait() : impl_(std::make_unique<Impl>()) {}

SerialPortWait::~SerialPortWait() { Close(); }

bool SerialPortWait::Open(int port_fd, int stop_fd) {
    Close();
    const int queue = ::kqueue();
    if (queue < 0) return false;
    if (stop_fd >= 0) {
        struct kevent change;
        EV_SET(&change, static_cast<uintptr_t>(stop_fd), EVFILT_READ, EV_ADD, 0u, 0, nullptr);
        if (::kevent(queue, &change, 1, nullptr, 0, nullptr) != 0) {
            ::close(queue);
            return false;
        }
    }
    impl_->port  = port_fd;
    impl_->stop  = stop_fd;
    impl_->queue = queue;
    return true;
}

void SerialPortWait::Close() {
    if (impl_->queue >= 0) ::close(impl_->queue);
    impl_->queue = -1;
    impl_->port  = -1;
    impl_->stop  = -1;
}

SerialWaitResult SerialPortWait::WaitReadable(uint32_t timeout_ms) {
    return Wait(impl_->queue, impl_->port, impl_->stop, EVFILT_READ, timeout_ms);
}

SerialWaitResult SerialPortWait::WaitWritable(uint32_t timeout_ms) {
    return Wait(impl_->queue, impl_->port, impl_->stop, EVFILT_WRITE, timeout_ms);
}

}

}
