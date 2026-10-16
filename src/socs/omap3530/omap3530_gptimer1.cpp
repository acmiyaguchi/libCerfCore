#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../cycle_anchored_counter.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../irq_controller.h"
#include <cerfcore/state/state_stream.h>
#include "omap3530_clocks.h"
#include "omap3530_gptimer1_regs.h"

#include <cstdint>

namespace {

using namespace Omap3530Gptimer1Regs;

class Omap3530Gptimer1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
    }

    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        irq_   = &emu_.Get<IrqController>();
        event_ = clock_->Add([this] { OnEvent(); });
        counter_.Anchor(clock_->Cycles(), 0u);
        ApplyRatio();
        ResetState();
        clock_->RegisterRateListener([this] { OnRateChange(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetState();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kGptimer1BasePa; }
    uint32_t MmioSize() const override { return kGptimer1Size; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override { PublishIrqLine(IrqLevel()); }

private:
    enum class Pending : uint8_t { kNone, kMatch, kOverflow };

    void ApplyRatio() {
        if (!counter_.SetRatio(clock_->CpuHz(), kOmap3530Clk32kHz)) RatioOverflow();
    }

    void OnRateChange() {
        const uint64_t now = clock_->Cycles();
        if (!counter_.Rescale(now, clock_->CpuHz(), kOmap3530Clk32kHz)) RatioOverflow();
        Arm(now);
    }

    [[noreturn]] void RatioOverflow() const {
        emu_.Get<Fatal>().Die(
            "omap3530 gptimer1: the %llu Hz timer clock against the %llu Hz core "
            "overflows the 64-bit scale",
            static_cast<unsigned long long>(kOmap3530Clk32kHz),
            static_cast<unsigned long long>(clock_->CpuHz()));
    }

    uint32_t Count(uint64_t cycle) const {
        return running_ ? counter_.CountAt(cycle) : stopped_count_;
    }

    /* §16.2.4.4 (printed p. 2611): with CE set, TCRR "is continuously compared"
       to TMAR, TMAR "can be loaded at any time", and when the two "values match,
       an interrupt is issued". */
    void MatchAtUpdate(uint64_t cycle, const char* what) {
        if ((tclr_ & kTclrCe) == 0u || tmar_ != Count(cycle)) return;
        if (!running_) {
            emu_.Get<Fatal>().Die(
                "omap3530 gptimer1: %s leaves TMAR 0x%08X equal to the stopped TCRR "
                "with CE set; whether a stopped comparator matches is not modelled",
                what, tmar_);
        }
        tisr_ |= kIntMat;
    }

    /* §16.2.4 (printed p. 2605): a free-running upward counter with autoreload
       on overflow, plus compare logic against GPTi.TMAR. */
    void Arm(uint64_t ref) {
        if (!running_) {
            pending_ = Pending::kNone;
            clock_->Disarm(event_);
            return;
        }
        const uint32_t count = counter_.CountAt(ref);
        uint64_t ticks = kCounterModulo - count;
        pending_       = Pending::kOverflow;
        if ((tclr_ & kTclrCe) != 0u && tmar_ > count) {
            ticks    = tmar_ - count;
            pending_ = Pending::kMatch;
        }
        armed_cycle_ = counter_.CycleOfTick(counter_.TicksSince(ref) + ticks);
        clock_->Arm(event_, armed_cycle_);
    }

    void OnEvent();
    void OnOverflow(uint64_t edge);
    void ResetFunctional();
    void ResetState();
    void WriteTclr(uint32_t value, uint64_t now);
    void LoadCounter(uint32_t value, uint64_t now);

    bool IrqLevel() const { return (tisr_ & tier_ & kIntMask) != 0u; }

    void PublishIrqLine(bool high) {
        irq_high_ = high;
        if (high) irq_->AssertIrq  (kIrqGptimer1);
        else      irq_->DeAssertIrq(kIrqGptimer1);
    }

    void DriveIrqLine() {
        const bool high = IrqLevel();
        if (high != irq_high_) PublishIrqLine(high);
    }

    GuestCycleClock*           clock_ = nullptr;
    IrqController*             irq_   = nullptr;
    GuestCycleClock::Event*    event_ = nullptr;
    NarrowCycleAnchoredCounter counter_;

    uint32_t tisr_          = 0;
    uint32_t tier_          = 0;
    uint32_t tclr_          = 0;
    uint32_t tldr_          = 0;
    uint32_t tmar_          = 0;
    uint32_t tsicr_         = 0;
    uint32_t stopped_count_ = 0;
    bool     running_       = false;
    bool     one_shot_done_ = false;
    bool     irq_high_      = false;
    Pending  pending_       = Pending::kNone;
    uint64_t armed_cycle_   = 0;
};

void Omap3530Gptimer1::OnEvent() {
    const Pending  kind = pending_;
    const uint64_t edge = armed_cycle_;
    pending_ = Pending::kNone;
    switch (kind) {
    case Pending::kMatch:
        tisr_ |= kIntMat;
        break;
    case Pending::kOverflow:
        OnOverflow(edge);
        break;
    case Pending::kNone:
        emu_.Get<Fatal>().Die("omap3530 gptimer1: timer event fired with no "
                              "pending overflow or match");
    }
    Arm(edge);
    DriveIrqLine();
}

void Omap3530Gptimer1::OnOverflow(uint64_t edge) {
    tisr_ |= kIntOvf;
    if ((tclr_ & kTclrAr) != 0u) {
        if (tldr_ == kTldrOverflowValue) {
            emu_.Get<Fatal>().Die("omap3530 gptimer1: autoreload with TLDR "
                                  "0xFFFFFFFF is not modelled");
        }
        counter_.SetCountAt(edge, tldr_);
        MatchAtUpdate(edge, "the overflow reload");
        return;
    }
    running_       = false;
    stopped_count_ = 0u;
    one_shot_done_ = true;
    if ((tclr_ & kTclrCe) != 0u && tmar_ == 0u) {
        emu_.Get<Fatal>().Die("omap3530 gptimer1: a one-shot overflow stops TCRR "
                              "at TMAR 0 with CE set; whether that matches is "
                              "not modelled");
    }
}

/* §16.2.4.2 (printed p. 2607): "The timer is stopped and the counter value is
   set to 0 when the module reset is asserted. The timer is maintained at stop
   after the reset is released." */
void Omap3530Gptimer1::ResetFunctional() {
    tisr_          = 0;
    tier_          = 0;
    tclr_          = 0;
    tldr_          = 0;
    /* Table 16-38 (printed p. 2635): TMAR COMPARE_VALUE [31:0] reset
       0x00000000. */
    tmar_          = 0;
    stopped_count_ = 0;
    running_       = false;
    one_shot_done_ = false;
    pending_       = Pending::kNone;
    clock_->Disarm(event_);
}

void Omap3530Gptimer1::ResetState() {
    ResetFunctional();
    tsicr_ = kTsicrPosted;
    PublishIrqLine(IrqLevel());
}

/* §16.2.4.2 (printed p. 2607): the counter "can be started and stopped at any
   time through the timer control register (GPTi.TCLR[0] ST bit)". */
void Omap3530Gptimer1::WriteTclr(uint32_t value, uint64_t now) {
    if ((value & kTclrPinFields) != 0u) {
        HaltUnsupportedAccess("WriteWord(TCLR capture / PWM-out pin field)",
                              kGptimer1BasePa + kOffTclr, value);
    }
    if ((value & kTclrPre) != 0u) {
        emu_.Get<Fatal>().Die("omap3530 gptimer1: TCLR write 0x%08X enables the "
                              "prescaler; its phase is not modelled", value);
    }
    const uint32_t count = Count(now);
    tclr_ = value & kTclrMask;
    if ((tclr_ & kTclrSt) == 0u) {
        stopped_count_ = count;
        running_       = false;
        one_shot_done_ = false;
    } else if (one_shot_done_) {
        emu_.Get<Fatal>().Die("omap3530 gptimer1: TCLR write 0x%08X keeps ST set after a "
                              "one-shot overflow stop; whether it restarts the counter is "
                              "not modelled", value);
    } else if (!running_) {
        running_ = true;
        counter_.SetCountAt(now, stopped_count_);
    }
    MatchAtUpdate(now, "TCLR write");
    Arm(now);
    DriveIrqLine();
}

void Omap3530Gptimer1::LoadCounter(uint32_t value, uint64_t now) {
    if (one_shot_done_) {
        emu_.Get<Fatal>().Die("omap3530 gptimer1: a TCRR / TTGR load of 0x%08X after a "
                              "one-shot overflow stop; whether it restarts the counter is "
                              "not modelled", value);
    }
    running_       = (tclr_ & kTclrSt) != 0u;
    if (running_) counter_.SetCountAt(now, value);
    else          stopped_count_ = value;
    MatchAtUpdate(now, "TCRR load");
    Arm(now);
    DriveIrqLine();
}

uint32_t Omap3530Gptimer1::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();

    switch (off) {
    /* Table 16-16 (printed p. 2621): TIDR TID_REV [7:0] R, whose reset value the
       manual gives as TI internal data; bits [31:8] Reserved, "Reads return
       0". */
    case kOffTidr:    return 0u;
    /* Table 16-20 (printed p. 2624): TISTAT RESETDONE [0] R, "0x1: Reset
       completed"; bits [31:8] and [7:1] Reserved, "Reads return 0". */
    case kOffTistat:  return 0x1u;
    case kOffTisr:    return tisr_ & kIntMask;
    case kOffTier:    return tier_ & kIntMask;
    case kOffTclr:    return tclr_;
    /* §16.2.4.2 (printed p. 2607): the counter "value can be read when stopped
       or captured on-the-fly by a GPTi.TCRR read access". */
    case kOffTcrr:    return Count(now);
    case kOffTldr:    return tldr_;
    /* Table 16-34 (printed p. 2632): TTGR_VALUE [31:0] - "The value of the
       trigger register. During reads, it always returns 0xFFFFFFFF." */
    case kOffTtgr:    return 0xFFFFFFFFu;
    /* Table 16-36 (printed p. 2633): TWPS is type R and "indicates if a
       Write-Posted is pending"; every W_PEND_* field is R reset 0 and bits
       [31:10] are Reserved, "Reads return 0". */
    case kOffTwps:    return 0u;
    case kOffTmar:    return tmar_;
    case kOffTsicr:   return tsicr_;
    /* Table 16-40 (printed p. 2636) / Table 16-44 (printed p. 2638): TCAR1 and
       TCAR2 are type R, reset 0x00000000. */
    case kOffTcar1:   return 0u;
    case kOffTcar2:   return 0u;
    case kOffTpir:    return 0u;
    case kOffTnir:    return 0u;
    case kOffTcvr:    return 0u;
    case kOffTocr:    return 0u;
    case kOffTowr:    return 0u;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Omap3530Gptimer1::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();

    switch (off) {
    /* §16.2.6.1 (printed p. 2615): the host-writable set is TLDR, TCRR, TIER,
       TISR, TCLR, TIOCP_CFG, TWER, TTGR, TSICR, TMAR and, on GPTIMER1, TPIR,
       TNIR, TCVR, TOCR, TOWR; TIDR, TISTAT, TWPS, TCAR1 and TCAR2 are not. */
    case kOffTidr:
    case kOffTistat:
    case kOffTwps:
    case kOffTcar1:
    case kOffTcar2:
        return;
    case kOffTiocp:
        if (value & kTiocpSoftReset) ResetState();
        return;
    case kOffTisr:
        tisr_ &= ~(value & kIntMask);
        DriveIrqLine();
        return;
    case kOffTier:
        tier_ = value & kIntMask;
        LOG(Periph, "[GPTIMER1] TIER <- 0x%X (MAT=%d OVF=%d)\n",
            tier_, (tier_ & kIntMat) ? 1 : 0, (tier_ & kIntOvf) ? 1 : 0);
        DriveIrqLine();
        return;
    /* Table 16-26 (printed p. 2627): TWER "controls (enable/disable) the
       wake-up feature on specific interrupt events" - MAT_WUP_ENA [0],
       OVF_WUP_ENA [1], TCAR_WUP_ENA [2]. */
    case kOffTwer:
        return;
    case kOffTclr:
        WriteTclr(value, now);
        return;
    case kOffTcrr:
        LoadCounter(value, now);
        return;
    case kOffTldr:
        tldr_ = value;
        return;
    case kOffTtgr:
        LoadCounter(tldr_, now);
        return;
    case kOffTmar:
        tmar_ = value;
        MatchAtUpdate(now, "TMAR write");
        Arm(now);
        DriveIrqLine();
        return;
    case kOffTsicr:
        if (value & kTsicrSft) {
            ResetFunctional();
            DriveIrqLine();
        }
        tsicr_ = value & kTsicrPosted;
        return;
    /* §16.2.4.2.1 (printed p. 2610): these registers drive the 1-ms tick
       generation and the overflow interrupt filter, whose reset state is
       all zeros with "no action on the programming model". */
    case kOffTpir:
    case kOffTnir:
    case kOffTcvr:
    case kOffTocr:
    case kOffTowr:
        if (value != 0) {
            HaltUnsupportedAccess(
                "WriteWord(1-ms tick / overflow-filter register)", addr, value);
        }
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Omap3530Gptimer1::SaveState(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    w.Write("tisr", tisr_);
    w.Write("tier", tier_);
    w.Write("tclr", tclr_);
    w.Write("tldr", tldr_);
    w.Write("tmar", tmar_);
    w.Write("tsicr", tsicr_);
    w.Write<uint32_t>("counter", Count(now));
    w.Write<uint8_t>("running", running_ ? 1u : 0u);
    w.Write<uint8_t>("one_shot_done", one_shot_done_ ? 1u : 0u);
    w.Write<uint64_t>("clk32k_phase", counter_.PhaseAt(now));
    w.Write<uint64_t>("clk32k_phase_den", counter_.PhaseDenominator());
}

void Omap3530Gptimer1::RestoreState(StateReader& r) {
    uint32_t tisr = 0, tier = 0, tclr = 0, tldr = 0, tmar = 0, tsicr = 0;
    uint32_t counter = 0;
    uint8_t  running = 0, one_shot_done = 0;
    uint64_t phase = 0, phase_den = 0;
    r.Read("tisr", tisr);
    r.Read("tier", tier);
    r.Read("tclr", tclr);
    r.Read("tldr", tldr);
    r.Read("tmar", tmar);
    r.Read("tsicr", tsicr);
    r.Read("counter", counter);
    r.Read("running", running);
    r.Read("one_shot_done", one_shot_done);
    r.Read("clk32k_phase", phase);
    r.Read("clk32k_phase_den", phase_den);
    if ((tisr & ~kIntMask) != 0u || (tier & ~kIntMask) != 0u) {
        r.Reject("omap3530 gptimer1: restored TISR 0x%08X / TIER 0x%08X set "
                 "reserved bits", tisr, tier);
    }
    if ((tclr & ~kTclrMask) != 0u || (tclr & (kTclrPinFields | kTclrPre)) != 0u) {
        r.Reject("omap3530 gptimer1: restored TCLR 0x%08X sets a field this "
                 "build does not model", tclr);
    }
    if ((tsicr & ~kTsicrPosted) != 0u) {
        r.Reject("omap3530 gptimer1: restored TSICR 0x%08X sets reserved bits",
                 tsicr);
    }
    if (running > 1u || one_shot_done > 1u) {
        r.Reject("omap3530 gptimer1: restored run flags %u / %u are not 0 or 1",
                 running, one_shot_done);
    }
    if (running != 0u && ((tclr & kTclrSt) == 0u || one_shot_done != 0u)) {
        r.Reject("omap3530 gptimer1: restored counter runs with TCLR 0x%08X and "
                 "one-shot-done %u", tclr, one_shot_done);
    }
    if (running != 0u && tldr == kTldrOverflowValue && (tclr & kTclrAr) != 0u) {
        r.Reject("omap3530 gptimer1: restored autoreload with TLDR 0xFFFFFFFF");
    }
    tisr_          = tisr;
    tier_          = tier;
    tclr_          = tclr;
    tldr_          = tldr;
    tmar_          = tmar;
    tsicr_         = tsicr;
    running_       = running != 0u;
    one_shot_done_ = one_shot_done != 0u;
    stopped_count_ = running_ ? 0u : counter;
    const uint64_t now = clock_->Cycles();
    ApplyRatio();
    if (!counter_.AnchorAtPhase(now, running_ ? counter : 0u, phase, phase_den)) {
        r.Reject("omap3530 gptimer1: restored 32-kHz phase %llu/%llu is not a "
                 "fraction of one tick this build can place",
                 static_cast<unsigned long long>(phase),
                 static_cast<unsigned long long>(phase_den));
    }
    Arm(now);
}

}

REGISTER_SERVICE(Omap3530Gptimer1);
