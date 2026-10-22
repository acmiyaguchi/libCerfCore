#pragma once

#include <atomic>
#include <cstdint>

namespace cerfcore {

struct WaveInFormat {
    uint32_t rate_hz  = 0;
    uint16_t channels = 0;
    uint16_t bits     = 0;
};

class WaveInRequest {
public:
    bool Set(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    bool Pending() const;
    bool TakeChange(WaveInFormat& format);

private:
    std::atomic<uint64_t> requested_{0};
    uint64_t              open_ = 0;
};

}
