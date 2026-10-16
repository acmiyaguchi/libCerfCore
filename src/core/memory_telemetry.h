#pragma once
#include <cerfcore/core/service.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace cerfcore {

class MemoryTelemetry : public Service {
public:
    explicit MemoryTelemetry(CerfEmulator& emu) : Service(emu) {}
    ~MemoryTelemetry() override;

    void OnReady() override;

private:
    static void ThreadMain(MemoryTelemetry* self);
    void Sample();

    std::thread             thread_;
    std::mutex              stop_mutex_;
    std::condition_variable stop_cv_;
    bool                    stop_ = false;
    std::atomic<uint64_t>   peak_private_bytes_{0};

    static constexpr std::chrono::milliseconds kPollInterval{2000};
};

}
