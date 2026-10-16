#include "sa11xx_mcp_frame_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_external_clocks.h"
#include "sa11xx_mcp_regs.h"

namespace cerfcore {

using namespace sa11xx_mcp;

namespace {

/* SA-1110 Table 11-5 (printed 11-5): GPIO 21 "Clock input pin for MCP". */
constexpr uint32_t kMcpClockPin = 21u;

}

bool Sa11xxMcpFrameClock::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxMcpFrameClock::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    ext_   = &emu_.Get<Sa11xxExternalClocks>();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        Disable();
        mccr0_ &= ~kMce;
    });
}

void Sa11xxMcpFrameClock::SetControl(uint32_t mccr0, uint32_t mccr1) {
    mccr0_ = mccr0;
    mccr1_ = mccr1;
}

/* §11.12.1: "dividing either by 5 (9.58464 MHz) or by 4 (11.9808 MHz)" the 3.6864 MHz oscillator
   multiplied by 13; MCCR1 CFS 1 = 9.585 MHz. */
uint64_t Sa11xxMcpFrameClock::SclkHzFor(uint32_t mccr1) {
    return (mccr1 & kCfs) != 0u ? 9584640u : 11980800u;
}

bool Sa11xxMcpFrameClock::Mce() const {
    return (mccr0_ & kMce) != 0u;
}

/* §11.12.3.4 (printed 11-135): "When ECS=1, the MCP uses GPIO 21 to input a clock supplied from
   off-chip"; §11.12.3.11 (printed 11-137): ECP divides "the clock input on the GPIO 21 pin by 1, 2,
   3, or 4". */
bool Sa11xxMcpFrameClock::SclkRateFor(uint32_t mccr0, uint32_t mccr1,
                                      GuestCycleClock::Rate& rate) const {
    if ((mccr0 & kEcs) == 0u) {
        rate = GuestCycleClock::Rate{SclkHzFor(mccr1), 1u};
        return true;
    }
    GuestCycleClock::Rate pin{};
    if (!ext_->Rate(kMcpClockPin, pin)) return false;
    rate = pin.Scaled(1u, ((mccr0 >> kEcpShift) & kEcpMask) + 1u);
    return true;
}

GuestCycleClock::Rate Sa11xxMcpFrameClock::WordClock() const {
    return rate_.num != 0u ? rate_ : GuestCycleClock::Rate{SclkHz(), 1u};
}

bool Sa11xxMcpFrameClock::SclkTick(uint64_t now, uint64_t& tick) const {
    if (!enabled_) return false;
    tick = sclk_.TicksAt(now);
    return true;
}

uint64_t Sa11xxMcpFrameClock::NextFrameEdge(uint64_t now) const {
    return (SclkNow(now) / kFrameTicks + 1u) * kFrameTicks;
}

void Sa11xxMcpFrameClock::Disable() {
    enabled_     = false;
    held_        = false;
    frozen_tick_ = 0u;
}

/* §11.12.3.3 (printed 11-135): "When the MCP is disabled, all of its clocks are powered down";
   "the current frame, which is being transmitted, [is] terminated". */
void Sa11xxMcpFrameClock::Freeze(uint64_t now) {
    frozen_tick_ = SclkNow(now);
    enabled_     = false;
    held_        = false;
}

/* §11.12.1.1: "After the MCP is enabled, SCLK begins to transition at the programmed clock rate
   and the start of the first frame is signalled by pulsing the SFRM". */
bool Sa11xxMcpFrameClock::Resume(uint64_t now) {
    GuestCycleClock::Rate rate{};
    if (!SclkRateFor(mccr0_, mccr1_, rate)) return false;
    const uint64_t base = (frozen_tick_ / kFrameTicks + 1u) * kFrameTicks;
    if (!sclk_.SetRate(clock_->ClockRate(), rate)) {
        emu_.Get<Fatal>().Die("Sa11xxMcp: SCLK rate overflows the cycle clock ratio");
    }
    rate_ = rate;
    sclk_.Start(now);
    sclk_.AddTicks(base);
    run_base_ = base;
    enabled_  = true;
    return true;
}

void Sa11xxMcpFrameClock::Retime(uint64_t now) {
    GuestCycleClock::Rate rate{};
    const bool runs = SclkRateFor(mccr0_, mccr1_, rate);
    if (enabled_) {
        if (runs && rate == rate_) return;
        held_pos_    = sclk_.PositionAt(now);
        frozen_tick_ = held_pos_.ticks;
        held_        = true;
        enabled_     = false;
    }
    if (!runs || !held_) return;
    if (!sclk_.SetRate(clock_->ClockRate(), rate) || !sclk_.PlaceAt(now, held_pos_)) {
        emu_.Get<Fatal>().Die("Sa11xxMcp: SCLK rate overflows the cycle clock ratio");
    }
    rate_    = rate;
    held_    = false;
    enabled_ = true;
}

void Sa11xxMcpFrameClock::OnCpuRate(uint64_t now) {
    if (!enabled_) return;
    if (!sclk_.Rescale(now, clock_->ClockRate(), rate_)) {
        emu_.Get<Fatal>().Die("Sa11xxMcp: SCLK rate overflows the cycle clock ratio");
    }
}

void Sa11xxMcpFrameClock::Save(StateWriter& w, uint64_t now) const {
    const TickPosition pos =
        enabled_ ? sclk_.PositionAt(now) : (held_ ? held_pos_ : TickPosition{});
    w.Write<uint8_t>("sclk_enabled", enabled_ ? 1u : 0u);
    w.Write<uint8_t>("sclk_held", held_ ? 1u : 0u);
    w.Write<uint64_t>("sclk_rate_num", rate_.num);
    w.Write<uint64_t>("sclk_rate_den", rate_.den);
    w.Write<uint32_t>("sclk_mccr0", mccr0_);
    w.Write<uint32_t>("sclk_mccr1", mccr1_);
    w.Write<uint64_t>("sclk_run_base", run_base_);
    w.Write<uint64_t>("sclk_frozen_tick", frozen_tick_);
    w.Write<uint64_t>("sclk_ticks", pos.ticks);
    w.Write<uint64_t>("sclk_phase", pos.phase);
    w.Write<uint64_t>("sclk_phase_den", pos.phase_den);
}

void Sa11xxMcpFrameClock::Restore(StateReader& r) {
    uint8_t enabled = 0, held = 0;
    TickPosition pos;
    r.Read("sclk_enabled", enabled);
    r.Read("sclk_held", held);
    r.Read("sclk_rate_num", rate_.num);
    r.Read("sclk_rate_den", rate_.den);
    r.Read("sclk_mccr0", mccr0_);
    r.Read("sclk_mccr1", mccr1_);
    r.Read("sclk_run_base", run_base_);
    r.Read("sclk_frozen_tick", frozen_tick_);
    r.Read("sclk_ticks", pos.ticks);
    r.Read("sclk_phase", pos.phase);
    r.Read("sclk_phase_den", pos.phase_den);
    enabled_  = enabled != 0u;
    held_     = held != 0u;
    held_pos_ = held_ ? pos : TickPosition{};
    if (!enabled_) return;
    sclk_.SetRate(clock_->ClockRate(), rate_);
    sclk_.PlaceAt(clock_->Cycles(), pos);
}

REGISTER_SERVICE(Sa11xxMcpFrameClock);

}
