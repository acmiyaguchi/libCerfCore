#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace cerfcore {

struct WaveOutFormat {
    uint32_t rate_hz         = 0;
    uint16_t channels        = 0;
    uint16_t bits            = 0;
    bool     allow_resampler = false;
};

class WaveOutDevice {
public:
    virtual bool DeviceOpenLocked() const                            = 0;
    virtual void CloseDeviceLocked(std::unique_lock<std::mutex>& lk) = 0;
    virtual bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk)  = 0;

protected:
    ~WaveOutDevice() = default;
};

class WaveOutReopenPolicy {
public:
    using Clock = std::chrono::steady_clock;

    bool EnsureFormat(const WaveOutFormat& format, bool busy, WaveOutDevice& device,
                      std::unique_lock<std::mutex>& lk);
    const WaveOutFormat& Requested() const { return requested_; }

    void NoteAttempt() { last_attempt_ = Clock::now(); }
    void NoteOpened() { opened_ = requested_; }

    bool BeginRetry(bool device_open);
    bool ShouldPostRetry();

private:
    static constexpr Clock::duration kReopenInterval = std::chrono::seconds(1);

    bool IntervalElapsed() const { return Clock::now() - last_attempt_ >= kReopenInterval; }

    WaveOutFormat     requested_;
    WaveOutFormat     opened_;
    Clock::time_point last_attempt_{};
    bool              retry_posted_ = false;
};

}
