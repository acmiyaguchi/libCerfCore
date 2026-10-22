#include "wave_out_reopen_policy.h"

namespace cerfcore {

namespace {

bool SameStream(const WaveOutFormat& a, const WaveOutFormat& b) {
    return a.rate_hz == b.rate_hz && a.channels == b.channels && a.bits == b.bits;
}

}

bool WaveOutReopenPolicy::EnsureFormat(const WaveOutFormat& format, bool busy,
                                       WaveOutDevice& device, std::unique_lock<std::mutex>& lk) {
    const bool changed = !SameStream(format, requested_) ||
                         format.allow_resampler != requested_.allow_resampler;
    requested_ = format;
    if (device.DeviceOpenLocked()) {
        if (SameStream(opened_, requested_)) return true;
        if (busy) return true;
        device.CloseDeviceLocked(lk);
    }
    if (!changed && !IntervalElapsed()) return false;
    return device.OpenDeviceLocked(lk);
}

bool WaveOutReopenPolicy::BeginRetry(bool device_open) {
    retry_posted_ = false;
    return !device_open && IntervalElapsed();
}

bool WaveOutReopenPolicy::ShouldPostRetry() {
    if (retry_posted_ || !IntervalElapsed()) return false;
    retry_posted_ = true;
    return true;
}

}
