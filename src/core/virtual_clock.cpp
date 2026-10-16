#include "virtual_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../platform/host_clock.h"

REGISTER_SERVICE(VirtualClock);

void VirtualClock::OnReady() {
    host_hz_ = host_platform::MonotonicFrequency();
    ns_per_tick_ = (host_hz_ > 0 && 1000000000ll % host_hz_ == 0)
                       ? 1000000000ll / host_hz_
                       : 0;
    epoch_ticks_.store(HostTicks(), std::memory_order_relaxed);
    accum_ticks_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
}

int64_t VirtualClock::HostTicks() const {
    return host_platform::MonotonicTicks();
}

void VirtualClock::RegisterEnableNotify(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(notify_mtx_);
    notify_.push_back(std::move(fn));
}

/* system/cpu-timers.c cpu_get_clock: the monotonic time elapsed in the VM,
   read under a seqlock. */
int64_t VirtualClock::NowNs() const {
    for (;;) {
        const uint32_t s0 = seq_.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u) {
            host_platform::CpuRelax();
            continue;
        }

        const int64_t accum   = accum_ticks_.load(std::memory_order_relaxed);
        const int64_t epoch   = epoch_ticks_.load(std::memory_order_relaxed);
        const bool    running = running_.load(std::memory_order_relaxed);
        const int64_t host    = HostTicks();

        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) != s0) {
            host_platform::CpuRelax();
            continue;
        }

        const int64_t ticks = running ? accum + (host - epoch) : accum;
        if (ns_per_tick_ != 0) return ticks * ns_per_tick_;
        return static_cast<int64_t>(ScaleU64(static_cast<uint64_t>(ticks),
                                             1000000000ull,
                                             static_cast<uint64_t>(host_hz_)));
    }
}

/* system/cpu-timers.c cpu_disable_ticks: latch the elapsed clock into the
   offset and stop it. */
void VirtualClock::Pause() {
    if (!running_.load(std::memory_order_relaxed)) return;

    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    const int64_t host = HostTicks();
    accum_ticks_.store(accum_ticks_.load(std::memory_order_relaxed) +
                           (host - epoch_ticks_.load(std::memory_order_relaxed)),
                       std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);
}

/* system/cpu-timers.c cpu_enable_ticks: re-anchor the offset and restart the
   clock. */
void VirtualClock::Resume() {
    if (running_.load(std::memory_order_relaxed)) return;

    seq_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    epoch_ticks_.store(HostTicks(), std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_relaxed);

    /* util/qemu-timer.c qemu_clock_enable: the enable edge runs
       qemu_clock_notify over the clock's timer lists. */
    std::vector<std::function<void()>> to_run;
    {
        std::lock_guard<std::mutex> lk(notify_mtx_);
        to_run = notify_;
    }
    for (const auto& fn : to_run) fn();
}
