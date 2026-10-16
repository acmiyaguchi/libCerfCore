#pragma once

#include "service.h"

#include <atomic>

namespace cerfengine { class GuestEngine; }

class Fatal : public Service {
public:
    using Service::Service;

    [[noreturn]] void Die(const char* fmt, ...);

    void SetLiveEngine(cerfengine::GuestEngine* engine) {
        live_engine_.store(engine, std::memory_order_release);
    }

private:
    std::atomic<cerfengine::GuestEngine*> live_engine_{nullptr};
};
