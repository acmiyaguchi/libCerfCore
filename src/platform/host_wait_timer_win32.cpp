#include "host_wait_timer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace host_platform {

HostWaitTimer::~HostWaitTimer() {
    Close();
}

bool HostWaitTimer::Create() {
    Close();
    HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                      TIMER_ALL_ACCESS);
    if (h == nullptr) h = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    handle_ = h;
    return h != nullptr;
}

void HostWaitTimer::Close() {
    if (handle_ == nullptr) return;
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
}

bool HostWaitTimer::Arm(int64_t ns) {
    LARGE_INTEGER due;
    due.QuadPart = -((ns + 99) / 100);
    return SetWaitableTimer(static_cast<HANDLE>(handle_), &due, 0, nullptr, nullptr, FALSE) != FALSE;
}

void HostWaitTimer::Wait() {
    WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);
}

bool HostWaitTimer::WaitOrEvent(void* event) {
    HANDLE objs[2] = { static_cast<HANDLE>(event), static_cast<HANDLE>(handle_) };
    return WaitForMultipleObjects(2, objs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
}

void HostWaitTimer::Cancel() {
    CancelWaitableTimer(static_cast<HANDLE>(handle_));
}

}
