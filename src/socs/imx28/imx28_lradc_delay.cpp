#include "imx28_lradc_delay.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint64_t kDelayClockHz = 2000u;

}

void Imx28LradcDelay::Init(std::function<void(uint32_t)> on_expiry) {
    clock_     = &emu_.Get<GuestCycleClock>();
    on_expiry_ = std::move(on_expiry);
    ticks_.Attach(kDelayClockHz, 1u);
    for (uint32_t d = 0; d < kChannels; ++d) {
        event_[d] = clock_->Add([this, d] { OnEvent(d); });
    }
}

void Imx28LradcDelay::Reset() {
    for (uint32_t d = 0; d < kChannels; ++d) Cancel(d);
}

void Imx28LradcDelay::Cancel(uint32_t channel) {
    armed_[channel] = 0u;
    clock_->Disarm(event_[channel]);
}

/* Section 38.5.14 KICK: "initiates a delay cycle"; LOOP_COUNT: 0 is exactly one delay loop,
   1 yields two. Section 38.2.4: "DELAY = 1, two ticks" at 2 kHz, loops spaced by the same delay. */
void Imx28LradcDelay::Kick(uint32_t channel, uint32_t delay, uint32_t loops) {
    const uint32_t count = delay + 1u;
    armed_[channel]  = 1u;
    count_[channel]  = count;
    loops_[channel]  = loops;
    target_[channel] = ticks_.Now() + count;
    ticks_.ArmAt(event_[channel], target_[channel]);
}

void Imx28LradcDelay::OnEvent(uint32_t channel) {
    if (armed_[channel] == 0u) return;
    if (--loops_[channel] != 0u) {
        target_[channel] += count_[channel];
        ticks_.ArmAt(event_[channel], target_[channel]);
    } else {
        armed_[channel] = 0u;
    }
    on_expiry_(channel);
}

void Imx28LradcDelay::Rescale() {
    ticks_.Rescale();
    Rearm();
}

void Imx28LradcDelay::Rearm() {
    for (uint32_t d = 0; d < kChannels; ++d) {
        if (armed_[d] != 0u) ticks_.ArmAt(event_[d], target_[d]);
        else                 clock_->Disarm(event_[d]);
    }
}

void Imx28LradcDelay::Save(StateWriter& w) {
    ticks_.Save(w, "delay_");
    w.Write("delay_armed", armed_);
    w.Write("delay_count", count_);
    w.Write("delay_loops", loops_);
    w.Write("delay_target", target_);
}

void Imx28LradcDelay::Restore(StateReader& r) {
    ticks_.Restore(r, "delay_");
    r.Read("delay_armed", armed_);
    r.Read("delay_count", count_);
    r.Read("delay_loops", loops_);
    r.Read("delay_target", target_);
    for (uint32_t d = 0; d < kChannels; ++d) clock_->Disarm(event_[d]);
}

}
