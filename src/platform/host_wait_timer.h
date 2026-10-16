#pragma once

#include <cstdint>

namespace cerfcore {

namespace host_platform {

class HostWaitTimer {
public:
    HostWaitTimer() = default;
    ~HostWaitTimer();
    HostWaitTimer(const HostWaitTimer&)            = delete;
    HostWaitTimer& operator=(const HostWaitTimer&) = delete;

    bool Create();
    void Close();
    bool Created() const { return handle_ != nullptr; }
    bool Arm(int64_t ns);
    void Wait();
    bool WaitOrEvent(void* event);
    void Cancel();
    void* NativeHandle() const { return handle_; }

private:
    void* handle_ = nullptr;
};

}

}
