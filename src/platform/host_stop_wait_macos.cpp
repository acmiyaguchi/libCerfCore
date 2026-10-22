#include "host_stop_wait.h"

#include "host_deadline_wait.h"

#include <sys/event.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

HostStopWait::~HostStopWait() { Close(); }

bool HostStopWait::Open(int stop_fd) {
    Close();
    const int queue = ::kqueue();
    if (queue < 0) return false;
    struct kevent change;
    EV_SET(&change, static_cast<uintptr_t>(stop_fd), EVFILT_READ, EV_ADD, 0u, 0, nullptr);
    if (::kevent(queue, &change, 1, nullptr, 0, nullptr) != 0) {
        ::close(queue);
        return false;
    }
    fd_ = queue;
    return true;
}

void HostStopWait::Close() {
    if (fd_ < 0) return;
    ::close(fd_);
    fd_ = -1;
}

bool HostStopWait::Sleep(int64_t ns) {
    return SleepUnlessReadable(ns, [this](const timespec& left) {
        struct kevent fired;
        return ::kevent(fd_, nullptr, 0, &fired, 1, &left);
    });
}

}

}
