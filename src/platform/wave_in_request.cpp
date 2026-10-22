#include "wave_in_request.h"

namespace cerfcore {

namespace {

uint64_t Key(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    return static_cast<uint64_t>(sample_rate_hz) | (static_cast<uint64_t>(channels) << 32) |
           (static_cast<uint64_t>(bits) << 48);
}

}

bool WaveInRequest::Set(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    const uint64_t key = Key(sample_rate_hz, channels, bits);
    return requested_.exchange(key) != key;
}

bool WaveInRequest::Pending() const { return requested_.load() != open_; }

bool WaveInRequest::TakeChange(WaveInFormat& format) {
    const uint64_t key = requested_.load();
    if (key == open_) return false;
    open_           = key;
    format.rate_hz  = static_cast<uint32_t>(key);
    format.channels = static_cast<uint16_t>(key >> 32);
    format.bits     = static_cast<uint16_t>(key >> 48);
    return true;
}

}
