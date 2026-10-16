#pragma once

#include <cstdint>

namespace cerfcore {

namespace host_platform {

class HostEvent {
public:
    HostEvent() = default;
    ~HostEvent();
    HostEvent(const HostEvent&)            = delete;
    HostEvent& operator=(const HostEvent&) = delete;

    bool Create();
    void Set();
    void Wait(uint32_t timeout_ms);

private:
    void* handle_ = nullptr;
};

}

}
