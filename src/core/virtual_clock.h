#pragma once

#include <cerfcore/core/service.h>
#include "tick_scale.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

class VirtualClock : public Service {
public:
    using Service::Service;

    void OnReady() override;

    int64_t NowNs() const;

    void Pause();
    void Resume();

    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }

    /* util/qemu-timer.c qemu_clock_enable: the enable edge notifies every
       timer list attached to the clock. */
    void RegisterEnableNotify(std::function<void()> fn);

private:
    int64_t HostTicks() const;

    std::atomic<uint32_t> seq_{0};
    std::atomic<int64_t>  accum_ticks_{0};
    std::atomic<int64_t>  epoch_ticks_{0};
    std::atomic<bool>     running_{true};
    int64_t               host_hz_     = 1;
    int64_t               ns_per_tick_ = 0;

    std::mutex                         notify_mtx_;
    std::vector<std::function<void()>> notify_;
};

}
