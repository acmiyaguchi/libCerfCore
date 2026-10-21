#pragma once

#include <chrono>
#include <cstdint>

namespace cerfcore {

class SilentBlockPacer {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr uint32_t kCapacity = 8;

    bool Queue(uintptr_t block, uint32_t length, uint32_t rate_hz, uint16_t channels,
               uint16_t bits, const char* log_tag);
    uint32_t TakeDue(Clock::time_point now, uintptr_t (&done)[kCapacity]);
    uint32_t TakeAll(uintptr_t (&done)[kCapacity]);

    bool              Empty() const { return count_ == 0; }
    Clock::time_point NextDue() const { return blocks_[0].due; }

private:
    struct Block {
        uintptr_t         id;
        Clock::time_point due;
    };

    Block             blocks_[kCapacity] = {};
    uint32_t          count_             = 0;
    Clock::time_point tail_{};
};

}
