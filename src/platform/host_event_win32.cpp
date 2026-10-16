#include "host_event.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cerfcore {

namespace host_platform {

HostEvent::~HostEvent() {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

bool HostEvent::Create() {
    handle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return handle_ != nullptr;
}

void HostEvent::Set() {
    SetEvent(static_cast<HANDLE>(handle_));
}

void HostEvent::Wait(uint32_t timeout_ms) {
    WaitForSingleObject(static_cast<HANDLE>(handle_), timeout_ms);
}

}

}
