#include "freescale_ssi_slot_grid.h"

#include <cerfcore/state/state_stream.h>
#include "freescale_ssi_regs.h"

#include <algorithm>
#include <bit>
#include <string>

namespace ssi = cerf_freescale_ssi;

bool FreescaleSsiFrameShape::Known() const {
    return bit_rate.num != 0u && bit_rate.den != 0u && slots != 0u && slots <= 32u && bits != 0u &&
           period != 0u;
}

/* MCIMX51RM Figure 56-35 (page 56-72) and MCIMX31RM Figure 45-42; / 4 with DIV2, PSR and PM all 0:
   MCIMX51RM Figure 56-35, i.MX31 Rockbox Gigabeat S i2s-gigabeat-s.c i2s_reset. Tables 56-17 /
   56-18 (pages 56-52 / 56-53): DC + 1 words, (WL + 1) x 2 bits, 32 bits in I2S Master. */
FreescaleSsiFrameShape FreescaleSsiSlotGrid::InternalClockShape(uint32_t scr, uint32_t ccr,
                                                                GuestCycleClock::Rate ssi_clk) {
    const uint32_t div2    = (ccr >> 18) & 0x1u;
    const uint32_t psr     = (ccr >> 17) & 0x1u;
    const uint32_t pm      = ccr & 0xFFu;
    const uint64_t bit_div = (div2 == 0u && psr == 0u && pm == 0u)
        ? 4u
        : static_cast<uint64_t>(div2 + 1u) * (7u * psr + 1u) * (pm + 1u) * 2u;
    const uint32_t i2s = (scr >> ssi::kScrI2sShift) & 0x3u;
    FreescaleSsiFrameShape shape;
    shape.bit_rate = ssi_clk.Scaled(1u, bit_div);
    shape.slots    = ((ccr >> 8) & 0x1Fu) + 1u;
    shape.bits     = i2s == ssi::kI2sMaster ? 32u : (((ccr >> 13) & 0xFu) + 1u) * 2u;
    return shape;
}

/* MCIMX31RM §45.1.2.1 (page 45-5): "If normal mode is configured with more than one time slot per
   frame, data is transferred only in the first time slot"; Table 45-22 STMSK (page 45-54): "1 Time
   slot is masked (no data transmitted in this time slot)". */
uint32_t FreescaleSsiSlotGrid::DataSlots(bool network, uint32_t slots, uint32_t mask) {
    const uint32_t all = slots >= 32u ? 0xFFFFFFFFu : (1u << slots) - 1u;
    return network ? (~mask & all) : 1u;
}

void FreescaleSsiSlotGrid::Attach() { ticks_.Attach(1u, 1u); }

bool FreescaleSsiSlotGrid::Apply(const FreescaleSsiFrameShape& shape) {
    if (!shape.Known()) {
        on_    = false;
        shape_ = shape;
        return true;
    }
    if (!on_ || shape.slots != shape_.slots || shape.bits != shape_.bits ||
        shape.tag_bits != shape_.tag_bits || shape.period != shape_.period) {
        shape_ = shape;
        on_    = true;
        ticks_.SetOscRate(shape.bit_rate.num, shape.bit_rate.den);
        ticks_.Rebase();
        return true;
    }
    ticks_.SetOscRate(shape.bit_rate.num, shape.bit_rate.den);
    shape_ = shape;
    return false;
}

void FreescaleSsiSlotGrid::OnCpuRate() {
    if (on_) ticks_.Rescale();
}

uint64_t FreescaleSsiSlotGrid::Started() {
    if (!on_) return 0u;
    uint64_t b = ticks_.Now();
    if (b < delay_) return 0u;
    b -= delay_;
    const uint64_t frame = b / shape_.FrameBits();
    const uint64_t r     = b % shape_.FrameBits();
    const uint64_t first = shape_.FirstBits();
    const uint64_t idx   = r < first ? 0u : 1u + (r - first) / shape_.bits;
    return frame * shape_.slots + idx + 1u;
}

uint64_t FreescaleSsiSlotGrid::SlotBit(uint64_t slot) const {
    const uint64_t frame = slot / shape_.slots;
    const uint64_t idx   = slot % shape_.slots;
    const uint64_t in    = idx == 0u ? 0u : shape_.FirstBits() + (idx - 1u) * shape_.bits;
    return frame * shape_.FrameBits() + in;
}

uint64_t FreescaleSsiSlotGrid::CycleOfSlot(uint64_t slot) {
    return ticks_.CycleOf(SlotBit(slot) + delay_);
}

uint64_t FreescaleSsiSlotGrid::NextFrame(uint64_t slot) const {
    const uint64_t frame = (slot + shape_.slots - 1u) / shape_.slots;
    return (frame + shape_.period - 1u) / shape_.period * shape_.period * shape_.slots;
}

/* MCIMX51RM Table 56-12 TE: "SSI expects 4 setup clock cycles before arrival of frame-sync for
   frame-sync to be accepted by SSI. In case of fewer clock cycles, there is high probability of
   the frame-sync to get missed." */
uint64_t FreescaleSsiSlotGrid::AcceptedFrame(uint64_t frame_slot) {
    if (setup_ == 0u || SlotBit(frame_slot) >= ticks_.Now() + 1u + setup_) return frame_slot;
    return frame_slot + static_cast<uint64_t>(shape_.period) * shape_.slots;
}

uint64_t FreescaleSsiSlotGrid::SlotAt(uint64_t from, uint32_t pos, uint64_t start,
                                      uint64_t stop) const {
    if (start == kNever) return kNever;
    const uint64_t a     = std::max(from, start);
    uint64_t       frame = a / shape_.slots;
    if (frame * shape_.slots + pos < a) ++frame;
    frame = (frame + shape_.period - 1u) / shape_.period * shape_.period;
    const uint64_t s = frame * shape_.slots + pos;
    return s < stop ? s : kNever;
}

/* MCIMX51RM Table 56-22 FRDIV (page 56-61): "the number of frames for which the SSI should be
   idle, after operating in one frame". */
uint64_t FreescaleSsiSlotGrid::DataSlotsBefore(uint32_t data, uint64_t slot) const {
    const uint64_t frame  = slot / shape_.slots;
    const uint32_t idx    = static_cast<uint32_t>(slot % shape_.slots);
    const uint64_t active = (frame + shape_.period - 1u) / shape_.period;
    const uint64_t part   = FrameActive(frame)
        ? static_cast<uint64_t>(std::popcount(data & ((1u << idx) - 1u)))
        : 0u;
    return active * static_cast<uint64_t>(std::popcount(data)) + part;
}

uint64_t FreescaleSsiSlotGrid::NthDataSlot(uint32_t data, uint64_t from, uint64_t n) const {
    const uint32_t per = static_cast<uint32_t>(std::popcount(data));
    if (per == 0u || n == 0u) return kNever;
    const uint64_t target = DataSlotsBefore(data, from) + n - 1u;
    uint32_t       bits   = data;
    for (uint32_t r = static_cast<uint32_t>(target % per); r != 0u; --r) bits &= bits - 1u;
    return (target / per) * shape_.period * shape_.slots +
           static_cast<uint64_t>(std::countr_zero(bits));
}

void FreescaleSsiSlotGrid::Save(StateWriter& w, const char* prefix) {
    const std::string p(prefix);
    w.Write<uint8_t>((p + "grid_on").c_str(), on_ ? 1u : 0u);
    w.Write<uint64_t>((p + "bit_num").c_str(), shape_.bit_rate.num);
    w.Write<uint64_t>((p + "bit_den").c_str(), shape_.bit_rate.den);
    w.Write<uint32_t>((p + "slots").c_str(), shape_.slots);
    w.Write<uint32_t>((p + "bits").c_str(), shape_.bits);
    w.Write<uint32_t>((p + "tag_bits").c_str(), shape_.tag_bits);
    w.Write<uint32_t>((p + "period").c_str(), shape_.period);
    ticks_.Save(w, prefix);
}

void FreescaleSsiSlotGrid::Restore(StateReader& r, const char* prefix) {
    const std::string p(prefix);
    uint8_t on = 0;
    r.Read((p + "grid_on").c_str(), on);
    r.Read((p + "bit_num").c_str(), shape_.bit_rate.num);
    r.Read((p + "bit_den").c_str(), shape_.bit_rate.den);
    r.Read((p + "slots").c_str(), shape_.slots);
    r.Read((p + "bits").c_str(), shape_.bits);
    r.Read((p + "tag_bits").c_str(), shape_.tag_bits);
    r.Read((p + "period").c_str(), shape_.period);
    ticks_.Restore(r, prefix);
    on_ = on != 0u;
}
