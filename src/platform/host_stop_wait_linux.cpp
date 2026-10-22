#include "host_stop_wait.h"

#include "host_deadline_wait.h"

#include <poll.h>

namespace cerfcore {

namespace host_platform {

HostStopWait::~HostStopWait() { Close(); }

bool HostStopWait::Open(int stop_fd) {
    fd_ = stop_fd;
    return true;
}

void HostStopWait::Close() { fd_ = -1; }

bool HostStopWait::Sleep(int64_t ns) {
    return SleepUnlessReadable(ns, [this](const timespec& left) {
        pollfd stop{fd_, POLLIN, 0};
        return ::ppoll(&stop, 1, &left, nullptr);
    });
}

}

}
