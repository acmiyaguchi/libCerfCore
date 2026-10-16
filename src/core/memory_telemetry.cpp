#include "memory_telemetry.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../platform/host_process_memory.h"

namespace cerfcore {

MemoryTelemetry::~MemoryTelemetry() {
    {
        std::lock_guard<std::mutex> lk(stop_mutex_);
        stop_ = true;
    }
    stop_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    uint64_t peak_mb = peak_private_bytes_.load(std::memory_order_acquire) / (1024 * 1024);
    LOG(Mem, "peak_private_mb=%llu\n", (unsigned long long)peak_mb);
}

void MemoryTelemetry::OnReady() {
    thread_ = std::thread(ThreadMain, this);
    LOG(Mem, "telemetry started (poll=%lldms)\n", (long long)kPollInterval.count());
}

void MemoryTelemetry::ThreadMain(MemoryTelemetry* self) {
    self->Sample();
    std::unique_lock<std::mutex> lk(self->stop_mutex_);
    while (!self->stop_cv_.wait_for(lk, kPollInterval, [self] { return self->stop_; })) {
        lk.unlock();
        self->Sample();
        lk.lock();
    }
}

void MemoryTelemetry::Sample() {
    const host_platform::ProcessMemoryUsage usage = host_platform::QueryProcessMemory();
    const uint64_t private_bytes = usage.private_bytes;
    uint64_t prev = peak_private_bytes_.load(std::memory_order_relaxed);
    while (private_bytes > prev &&
           !peak_private_bytes_.compare_exchange_weak(
               prev, private_bytes,
               std::memory_order_release,
               std::memory_order_relaxed)) {
    }

    const host_platform::ThreadStackUsage stacks = host_platform::MeasureThreadStacks();

    auto mb = [](uint64_t b) -> unsigned long long { return (unsigned long long)(b / (1024 * 1024)); };
    auto kb = [](uint64_t b) -> unsigned long long { return (unsigned long long)(b / 1024); };

    LOG(Mem,
        "private=%lluMB ws=%lluMB "
        "thread_stacks=%lluKB(%llu threads, reserve=%lluMB)\n",
        mb(private_bytes),
        mb(usage.working_set),
        kb(stacks.committed), (unsigned long long)stacks.count,
        mb(stacks.reserved));
}

REGISTER_SERVICE(MemoryTelemetry);

}
