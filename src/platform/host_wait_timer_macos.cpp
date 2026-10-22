#include "host_wait_timer.h"

#include <cerrno>
#include <sys/event.h>
#include <unistd.h>

#include <cerfengine/wake_event.h>

#include "host_fd_handle.h"

namespace cerfcore {

namespace host_platform {

namespace {

constexpr uintptr_t kTimerIdent = 1u;

enum class Fired { kTimer, kWake, kError };

int ApplyChange(int kq, uintptr_t ident, int16_t filter, uint16_t flags, uint32_t fflags,
                int64_t data) {
    struct kevent change;
    EV_SET(&change, ident, filter, flags, fflags, data, nullptr);
    return ::kevent(kq, &change, 1, nullptr, 0, nullptr);
}

Fired WaitFor(int kq, bool with_wake, uintptr_t wake_ident) {
    for (;;) {
        struct kevent out[2];
        const int     n = ::kevent(kq, nullptr, 0, out, 2, nullptr);
        if (n < 0) {
            if (errno == EINTR) continue;
            return Fired::kError;
        }
        bool timer = false;
        for (int i = 0; i < n; ++i) {
            if ((out[i].flags & EV_ERROR) != 0) return Fired::kError;
            if (with_wake && out[i].filter == EVFILT_READ && out[i].ident == wake_ident) {
                return Fired::kWake;
            }
            if (out[i].filter == EVFILT_TIMER) timer = true;
        }
        if (timer) return Fired::kTimer;
    }
}

}

HostWaitTimer::~HostWaitTimer() {
    Close();
}

bool HostWaitTimer::Create() {
    Close();
    const int kq = ::kqueue();
    if (kq < 0) return false;
    handle_ = HandleOf(kq);
    return true;
}

void HostWaitTimer::Close() {
    if (handle_ == nullptr) return;
    ::close(FdOf(handle_));
    handle_ = nullptr;
}

bool HostWaitTimer::Arm(int64_t ns) {
    return ApplyChange(FdOf(handle_), kTimerIdent, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
                       NOTE_NSECONDS | NOTE_CRITICAL, ns > 0 ? ns : 1) == 0;
}

void HostWaitTimer::Wait() {
    WaitFor(FdOf(handle_), false, 0u);
}

bool HostWaitTimer::WaitOrEvent(void* event) {
    const int       kq   = FdOf(handle_);
    const uintptr_t wake = static_cast<uintptr_t>(cerfengine::WakeEventDescriptor(event));
    if (ApplyChange(kq, wake, EVFILT_READ, EV_ADD, 0u, 0) != 0) return false;
    const Fired fired = WaitFor(kq, true, wake);
    ApplyChange(kq, wake, EVFILT_READ, EV_DELETE, 0u, 0);
    if (fired == Fired::kWake) cerfengine::ConsumeWakeEvent(event);
    return fired == Fired::kTimer;
}

void HostWaitTimer::Cancel() {
    ApplyChange(FdOf(handle_), kTimerIdent, EVFILT_TIMER, EV_DELETE, 0u, 0);
}

}

}
