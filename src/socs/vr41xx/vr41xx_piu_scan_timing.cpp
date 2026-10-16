#include "vr41xx_piu_scan_timing.h"

#include <algorithm>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>
#include "vr41xx_rtcx_ticks.h"

namespace {

constexpr uint64_t kAdConversionNs = 10000u;

}

void Vr41xxPiuScanTiming::Attach(GuestCycleClock* clock, Vr41xxRtcxTicks* rtcx) {
    clock_ = clock;
    rtcx_  = rtcx;
}

/* S = STABLE x 30 us, A = "about 10 us" (VR4121 UM Figure 20-5, VR4102 UM Figure 19-5); STABLE
   is the CmdScan stabilization time (VR4121 UM 20.3.4); CmdScan fetches "one port only" (20.2
   (4)). */
uint64_t Vr41xxPiuScanTiming::ReadyCycle(uint16_t kind, uint16_t stable, uint64_t start_tick,
                                         uint64_t start_cycle) {
    if (kind != kCmd) return start_cycle;
    const uint64_t settle = stable & 0x003Fu;
    const uint64_t settled =
        settle == 0u ? start_cycle : std::max(start_cycle, rtcx_->CycleOf(start_tick + settle));
    return settled + clock_->NsToCycles(static_cast<int64_t>(kAdConversionNs));
}

void Vr41xxPiuScanTiming::SetReady(uint64_t cycle) {
    const uint64_t now    = clock_->Cycles();
    const int64_t  now_ns = clock_->NowNs();
    ready_cycle_ = cycle;
    ready_ns_    = cycle >= now ? now_ns + clock_->CyclesToNs(cycle - now)
                                : now_ns - clock_->CyclesToNs(now - cycle);
}

void Vr41xxPiuScanTiming::Begin(uint16_t kind, uint16_t stable, uint64_t start_tick,
                                uint64_t start_cycle) {
    if (kind_ != kIdle) {
        emu_.Get<Fatal>().Die("VR41xx PIU: scan %u started while scan %u is in flight; "
                              "PADDLOSTINTR is not modeled", kind, kind_);
    }
    kind_ = kind;
    SetReady(ReadyCycle(kind, stable, start_tick, start_cycle));
}

uint16_t Vr41xxPiuScanTiming::Take() {
    const uint16_t kind = kind_;
    kind_ = kIdle;
    return kind;
}

void Vr41xxPiuScanTiming::Retime(uint64_t now) {
    if (kind_ == kIdle || ready_cycle_ <= now) return;
    const int64_t left = ready_ns_ - clock_->NowNs();
    ready_cycle_ = now + (left > 0 ? clock_->NsToCycles(left) : uint64_t{0});
}

void Vr41xxPiuScanTiming::Save(StateWriter& w, uint64_t now) const {
    const int64_t now_ns = clock_->NowNs();
    w.Write("scan_kind", kind_);
    w.Write("scan_ready_ns", kind_ != kIdle && ready_cycle_ > now && ready_ns_ > now_ns
                                 ? static_cast<uint64_t>(ready_ns_ - now_ns)
                                 : uint64_t{0});
}

void Vr41xxPiuScanTiming::Restore(StateReader& r, uint64_t now) {
    uint64_t ready_in = 0;
    r.Read("scan_kind", kind_);
    r.Read("scan_ready_ns", ready_in);
    ready_ns_    = clock_->NowNs() + static_cast<int64_t>(ready_in);
    ready_cycle_ = now + clock_->NsToCycles(static_cast<int64_t>(ready_in));
}
