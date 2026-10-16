#pragma once

#include "../execution/guest_cycle_clock.h"
#include "oscillator_ticks.h"

#include <cstdint>

class CerfEmulator;
class StateReader;
class StateWriter;

struct FreescaleSsiFrameShape {
    GuestCycleClock::Rate bit_rate{0u, 1u};
    uint32_t slots    = 0;
    uint32_t bits     = 0;
    uint32_t tag_bits = 0;
    uint32_t period   = 1;

    uint32_t FirstBits() const { return tag_bits != 0u ? tag_bits : bits; }
    uint32_t FrameBits() const { return FirstBits() + (slots - 1u) * bits; }
    bool     Known() const;
};

class FreescaleSsiSlotGrid {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    FreescaleSsiSlotGrid(CerfEmulator& emu, uint32_t setup_clocks, uint32_t flag_delay_bits)
        : setup_(setup_clocks), delay_(flag_delay_bits), ticks_(emu, false) {}

    static FreescaleSsiFrameShape InternalClockShape(uint32_t scr, uint32_t ccr,
                                                     GuestCycleClock::Rate ssi_clk);
    static uint32_t DataSlots(bool network, uint32_t slots, uint32_t mask);

    void Attach();
    void Stop() { on_ = false; }
    bool Apply(const FreescaleSsiFrameShape& shape);
    void OnCpuRate();

    bool Running() const { return on_; }
    uint32_t Slots() const { return shape_.slots; }

    uint64_t Started();
    uint64_t NowBit() { return ticks_.Now(); }
    uint64_t SlotBit(uint64_t slot) const;
    uint64_t CycleOfSlot(uint64_t slot);
    bool     FrameActive(uint64_t frame) const { return frame % shape_.period == 0u; }
    uint64_t NextFrame(uint64_t slot) const;
    uint64_t AcceptedFrame(uint64_t frame_slot);
    uint64_t SlotAt(uint64_t from, uint32_t pos, uint64_t start, uint64_t stop) const;

    uint64_t DataSlotsBefore(uint32_t data, uint64_t slot) const;
    uint64_t NthDataSlot(uint32_t data, uint64_t from, uint64_t n) const;

    void Save(StateWriter& w, const char* prefix);
    void Restore(StateReader& r, const char* prefix);

private:
    const uint32_t         setup_;
    const uint32_t         delay_;
    OscillatorTicks        ticks_;
    FreescaleSsiFrameShape shape_;
    bool                   on_ = false;
};
