#include "host_event.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr uint32_t kWaitForever = 0xFFFFFFFFu;

struct AutoResetEvent {
    std::mutex              mutex;
    std::condition_variable signaled_cv;
    bool                    signaled = false;
};

AutoResetEvent& EventOf(void* handle) { return *static_cast<AutoResetEvent*>(handle); }

}

HostEvent::~HostEvent() { delete static_cast<AutoResetEvent*>(handle_); }

bool HostEvent::Create() {
    handle_ = new AutoResetEvent;
    return true;
}

void HostEvent::Set() {
    AutoResetEvent& e = EventOf(handle_);
    {
        std::lock_guard<std::mutex> lock(e.mutex);
        e.signaled = true;
    }
    e.signaled_cv.notify_one();
}

void HostEvent::Wait(uint32_t timeout_ms) {
    AutoResetEvent&              e = EventOf(handle_);
    std::unique_lock<std::mutex> lock(e.mutex);
    if (timeout_ms == kWaitForever) {
        e.signaled_cv.wait(lock, [&e] { return e.signaled; });
    } else if (!e.signaled_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                       [&e] { return e.signaled; })) {
        return;
    }
    e.signaled = false;
}

}

}
