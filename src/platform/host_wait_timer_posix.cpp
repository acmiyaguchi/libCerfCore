#include "host_wait_timer.h"

#include <cerrno>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cerfengine/wake_event.h>

#include "host_fd_handle.h"

namespace cerfcore {

namespace host_platform {

namespace {

void Drain(int fd) {
    uint64_t count = 0;
    ssize_t  read_bytes;
    do {
        read_bytes = ::read(fd, &count, sizeof(count));
    } while (read_bytes < 0 && errno == EINTR);
}

}

HostWaitTimer::~HostWaitTimer() {
    Close();
}

bool HostWaitTimer::Create() {
    Close();
    const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (fd < 0) return false;
    handle_ = HandleOf(fd);
    return true;
}

void HostWaitTimer::Close() {
    if (handle_ == nullptr) return;
    ::close(FdOf(handle_));
    handle_ = nullptr;
}

bool HostWaitTimer::Arm(int64_t ns) {
    const int64_t due = ns > 0 ? ns : 1;
    itimerspec    spec{};
    spec.it_value.tv_sec  = static_cast<time_t>(due / 1000000000);
    spec.it_value.tv_nsec = static_cast<long>(due % 1000000000);
    return ::timerfd_settime(FdOf(handle_), 0, &spec, nullptr) == 0;
}

void HostWaitTimer::Wait() {
    Drain(FdOf(handle_));
}

bool HostWaitTimer::WaitOrEvent(void* event) {
    pollfd fds[2] = {{cerfengine::WakeEventDescriptor(event), POLLIN, 0},
                     {FdOf(handle_), POLLIN, 0}};
    for (;;) {
        const int ready = ::poll(fds, 2, -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return false;
        if ((fds[0].revents & POLLIN) != 0) {
            Drain(fds[0].fd);
            return false;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            Drain(fds[1].fd);
            return true;
        }
        if (((fds[0].revents | fds[1].revents) & (POLLERR | POLLHUP | POLLNVAL)) != 0) return false;
    }
}

void HostWaitTimer::Cancel() {
    const itimerspec spec{};
    ::timerfd_settime(FdOf(handle_), 0, &spec, nullptr);
}

}

}
