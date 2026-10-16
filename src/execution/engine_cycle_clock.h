#pragma once

#include <cstdint>

#include "guest_cycle_clock.h"
#include <cerfengine/cycle_deadline.h>

template <typename CpuState>
class EngineCycleClock : public GuestCycleClock {
public:
    using GuestCycleClock::GuestCycleClock;

protected:
    uint64_t Cycles() final {
        const uint32_t c = state_->guest_cycle_counter;
        if (c < state_->guest_cycle_folded) ++state_->guest_cycle_hi;
        state_->guest_cycle_folded = c;
        return (static_cast<uint64_t>(state_->guest_cycle_hi) << 32) | c;
    }

    void SetCycles(uint64_t cycles) final {
        state_->guest_cycle_hi      = static_cast<uint32_t>(cycles >> 32);
        state_->guest_cycle_counter = static_cast<uint32_t>(cycles);
        state_->guest_cycle_folded  = static_cast<uint32_t>(cycles);
    }

    void PublishDeadline(uint64_t cycles_ahead) final {
        state_->guest_cycle_deadline =
            cerfengine::CycleDeadline(state_->guest_cycle_counter, cycles_ahead);
    }

    CpuState* state_ = nullptr;
};
