#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../execution/guest_cycle_clock.h"
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "cycle_anchored_counter.h"
#include "freescale_timer_clocks.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace cerf_freescale_epit_detail {

constexpr uint32_t kEpitSize = 0x00004000u;

/* MCIMX31RM Table 33-5 / MCIMX51RM Table 29-2. */
constexpr uint32_t kOffCr   = 0x00u;
constexpr uint32_t kOffSr   = 0x04u;
constexpr uint32_t kOffLr   = 0x08u;
constexpr uint32_t kOffCmpr = 0x0Cu;
constexpr uint32_t kOffCnt  = 0x10u;

/* MCIMX31RM Table 33-6 / MCIMX51RM Table 29-5 EPITCR. */
constexpr uint32_t kCrEn             = 1u << 0;
constexpr uint32_t kCrEnmod          = 1u << 1;
constexpr uint32_t kCrOcien          = 1u << 2;
constexpr uint32_t kCrRld            = 1u << 3;
constexpr uint32_t kCrPrescalerShift = 4u;
constexpr uint32_t kCrPrescalerMask  = 0xFFFu;
constexpr uint32_t kCrSwr            = 1u << 16;
constexpr uint32_t kCrIovw           = 1u << 17;
constexpr uint32_t kCrWaiten         = 1u << 19;
constexpr uint32_t kCrDozen          = 1u << 20;
constexpr uint32_t kCrStopen         = 1u << 21;
constexpr uint32_t kCrOmShift        = 22u;
constexpr uint32_t kCrClksrcShift    = 24u;

/* MCIMX31RM Table 33-6: bits 31-26 reserved; SWR keeps EN, ENMOD, STOPEN, DOZEN,
   WAITEN and DBGEN. */
constexpr uint32_t kCrWritableMx31 = 0x03FFFFFFu;
constexpr uint32_t kCrSwrKeepMx31  = 0x003C0003u;
/* MCIMX51RM Table 29-5: bits 31-26 and 20 reserved; SWR keeps EN, ENMOD, STOPEN,
   WAITEN and DBGEN. */
constexpr uint32_t kCrWritableMx51 = 0x03EFFFFFu;
constexpr uint32_t kCrSwrKeepMx51  = 0x002C0003u;

/* MCIMX31RM Table 33-7 / MCIMX51RM Table 29-6: OCIF is w1c. */
constexpr uint32_t kSrOcif = 1u << 0;

/* MCIMX31RM Figures 33-6 and 33-8 / MCIMX51RM Table 29-2. */
constexpr uint32_t kLrReset  = 0xFFFFFFFFu;
constexpr uint32_t kCntReset = 0xFFFFFFFFu;

template <uint32_t kBase, const std::string_view& kSoc, FreescaleTimerUnit kUnit,
          uint32_t kCrWritable, uint32_t kCrSwrKeep>
class FreescaleEpitBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == kSoc;
    }

    void OnReady() override {
        clock_  = &emu_.Get<GuestCycleClock>();
        clocks_ = &emu_.Get<FreescaleTimerClocks>();
        event_  = clock_->Add([this] { OnCompare(); });
        clock_->RegisterRateListener([this] { Retime(); });
        clock_->RegisterIdleListener([this] { OnIdle(); });
        clock_->RegisterIdleExitListener([this] { OnIdleExit(); });
        clocks_->RegisterRateListener([this] { Retime(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            HardwareReset();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kEpitSize; }

    FastReadFn  FastReader() override { return &FreescaleEpitBase::FastReadThunk; }
    FastWriteFn FastWriter() override { return &FreescaleEpitBase::FastWriteThunk; }

    uint32_t ReadWord(uint32_t addr) override { return FastRead(addr - kBase, 4u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        FastWrite(addr - kBase, value, 4u);
    }

    void SaveState(StateWriter& w) override {
        const uint64_t now = clock_->Cycles();
        SettleOcif(now);
        w.Write<uint32_t>("cr", cr_);
        w.Write<uint32_t>("sr", sr_);
        w.Write<uint32_t>("lr", lr_);
        w.Write<uint32_t>("cmpr", cmpr_);
        w.Write<uint32_t>("count", Count(now));
        (counting_ ? counter_.TickPhaseAt(now) : phase_).Save(w, "prescale_tick_phase");
    }

    void RestoreState(StateReader& r) override {
        uint32_t cr = 0, sr = 0, lr = 0, cmpr = 0, count = 0;
        CycleAnchoredCounter::Phase phase;
        r.Read("cr", cr);
        r.Read("sr", sr);
        r.Read("lr", lr);
        r.Read("cmpr", cmpr);
        r.Read("count", count);
        phase.Restore(r, "prescale_tick_phase");
        clock_->Disarm(event_);
        ocif_pending_    = false;
        next_ocif_       = kNoOcif;
        counting_        = false;
        suspended_       = false;
        cr_              = cr;
        sr_              = sr;
        lr_              = lr;
        cmpr_            = cmpr;
        base_value_      = count;
        phase_           = phase;
    }

    void PostRestore() override {
        Retime();
        RefreshIrq();
    }

protected:
    virtual void AssertIrqLine()   = 0;
    virtual void DeassertIrqLine() = 0;

private:
    static uint32_t FastReadThunk(void* ctx, uint32_t off, uint32_t width) {
        return static_cast<FreescaleEpitBase*>(ctx)->FastRead(off, width);
    }
    static void FastWriteThunk(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
        static_cast<FreescaleEpitBase*>(ctx)->FastWrite(off, value, width);
    }

    static uint32_t Prescaler(uint32_t cr) {
        return (cr >> kCrPrescalerShift) & kCrPrescalerMask;
    }
    static uint32_t Clksrc(uint32_t cr) { return (cr >> kCrClksrcShift) & 0x3u; }
    static uint32_t Om(uint32_t cr) { return (cr >> kCrOmShift) & 0x3u; }

    uint32_t FastRead(uint32_t off, uint32_t width) {
        if (width != 4u) HaltUnsupportedAccess("FastRead", kBase + off, 0);
        switch (off) {
            case kOffCr:   return cr_;
            case kOffSr:   SettleOcif(clock_->Cycles()); return sr_;
            case kOffLr:   return lr_;
            case kOffCmpr: return cmpr_;
            case kOffCnt:  return Count(clock_->Cycles());
            default: break;
        }
        HaltUnsupportedAccess("FastRead", kBase + off, 0);
    }

    void FastWrite(uint32_t off, uint32_t value, uint32_t width) {
        if (width != 4u) HaltUnsupportedAccess("FastWrite", kBase + off, value);
        switch (off) {
            case kOffCr:   WriteCr(value); return;
            case kOffSr:   WriteSr(value); return;
            case kOffLr:   WriteLr(value); return;
            case kOffCmpr: WriteCmpr(value); return;
            default: break;
        }
        HaltUnsupportedAccess("FastWrite", kBase + off, value);
    }

    /* MCIMX31RM Figure 33-10 / MCIMX51RM Figure 29-9: in set-and-forget mode the
       counter holds 0 for one tick, then loads EPITLR. */
    uint32_t ValueAfter(uint64_t ticks) const {
        if ((cr_ & kCrRld) == 0u || ticks <= base_value_) {
            return base_value_ - static_cast<uint32_t>(ticks);
        }
        const uint64_t period = uint64_t{lr_} + 1u;
        return lr_ - static_cast<uint32_t>((ticks - base_value_ - 1u) % period);
    }

    uint64_t TicksAt(uint64_t now) const { return counter_.TicksSince(now) - base_tick_; }

    uint32_t Count(uint64_t now) const {
        return counting_ ? ValueAfter(TicksAt(now)) : base_value_;
    }

    bool NextCompareIndex(uint64_t ticks, uint64_t& e) const {
        if ((cr_ & kCrRld) == 0u) {
            const uint64_t d = static_cast<uint32_t>(base_value_ - cmpr_);
            e = ticks <= d ? d : d + (((ticks - d) + 0xFFFFFFFFull) >> 32 << 32);
            return true;
        }
        if (cmpr_ <= base_value_ && ticks <= uint64_t{base_value_ - cmpr_}) {
            e = base_value_ - cmpr_;
            return true;
        }
        if (cmpr_ > lr_) return false;
        const uint64_t period = uint64_t{lr_} + 1u;
        const uint64_t first  = uint64_t{base_value_} + 1u + (lr_ - cmpr_);
        e = ticks <= first ? first : first + (ticks - first + period - 1u) / period * period;
        return true;
    }

    /* MCIMX31RM Figure 33-10 / MCIMX51RM Figure 29-9: OCIF sets on the counter
       clock edge that moves the counter off the compare value. */
    void CaptureDue(uint64_t now) {
        if (now >= next_ocif_) ocif_pending_ = true;
    }

    void SettleOcif(uint64_t now) {
        CaptureDue(now);
        if (ocif_pending_ && (cr_ & kCrOcien) == 0u) Arm(now);
    }

    void Arm(uint64_t now) {
        uint64_t   e      = 0;
        const bool irq_on = (cr_ & kCrOcien) != 0u;
        if (ocif_pending_ && !irq_on) sr_ |= kSrOcif;
        ocif_pending_ = ocif_pending_ && irq_on;
        next_ocif_    = kNoOcif;
        if (ocif_pending_) {
            clock_->Arm(event_, now);
            return;
        }
        if (counting_ && (sr_ & kSrOcif) == 0u && NextCompareIndex(TicksAt(now), e)) {
            next_ocif_ = counter_.CycleOfTick(base_tick_ + e + 1u);
        }
        if (irq_on && next_ocif_ != kNoOcif) clock_->Arm(event_, next_ocif_);
        else                                 clock_->Disarm(event_);
    }

    void OnCompare() {
        if (!counting_ && !ocif_pending_) {
            emu_.Get<Fatal>().Die("EPIT %08X: compare event fired while the counter is "
                                  "stopped", kBase);
        }
        ocif_pending_ = false;
        next_ocif_    = kNoOcif;
        sr_ |= kSrOcif;
        RefreshIrq();
        Arm(clock_->Cycles());
    }

    void RefreshIrq() {
        if ((sr_ & kSrOcif) != 0u && (cr_ & kCrOcien) != 0u) AssertIrqLine();
        else                                                  DeassertIrqLine();
    }

    FreescaleTimerInput Input() const {
        switch (Clksrc(cr_)) {
            case 1u:  return FreescaleTimerInput::kIpg;
            case 2u:  return FreescaleTimerInput::kHighfreq;
            default:  return FreescaleTimerInput::kLowfreq;
        }
    }

    /* MCIMX31RM Table 33-6 / MCIMX51RM Table 29-5 CLKSRC 00: clock is off. */
    GuestCycleClock::Rate EnabledInputRate() const {
        if (suspended_ || (cr_ & kCrEn) == 0u || Clksrc(cr_) == 0u) {
            return GuestCycleClock::Rate{0u, 1u};
        }
        return clocks_->InputRate(kUnit, Input());
    }

    GuestCycleClock::Rate TickRate(GuestCycleClock::Rate input) const {
        return clocks_->PrescaledRate(kUnit, kBase, input, Prescaler(cr_));
    }

    void Rebase(uint64_t now) {
        base_value_ = Count(now);
        base_tick_  = counter_.TicksSince(now);
    }

    /* MCIMX31RM Table 33-6 / MCIMX51RM Table 29-5 ENMOD: with EN=0 the main
       counter and the prescaler counter freeze at their current values. */
    void Freeze(uint64_t now) {
        base_value_      = Count(now);
        phase_           = counter_.TickPhaseAt(now);
        counting_        = false;
    }

    /* MCIMX31RM Table 33-6 ENMOD and Figure 33-9, MCIMX51RM Table 29-5 ENMOD: after a
       prescaler counter reset the next prescaled pulse comes one input clock later. */
    void ResetPrescaler() {
        phase_ = CounterPhase::LastInputClock(uint64_t{Prescaler(cr_)} + 1u);
    }

    void Resume(uint64_t now, GuestCycleClock::Rate input) {
        const GuestCycleClock::Rate core = clock_->ClockRate();
        const GuestCycleClock::Rate tick = TickRate(input);
        counter_.SetRate(core, tick);
        counter_.AnchorAt(now, 0u, phase_);
        base_tick_  = 0u;
        ratio_core_ = core;
        ratio_tick_ = tick;
        counting_   = true;
    }

    void Retime() {
        const uint64_t              now   = clock_->Cycles();
        const GuestCycleClock::Rate input = EnabledInputRate();
        CaptureDue(now);
        if (!counting_) {
            if (input.num != 0u) Resume(now, input);
        } else if (input.num == 0u) {
            Freeze(now);
        } else {
            const GuestCycleClock::Rate core = clock_->ClockRate();
            const GuestCycleClock::Rate tick = TickRate(input);
            if (tick != ratio_tick_ || core != ratio_core_) {
                base_value_ = Count(now);
                counter_.RescaleRate(now, core, tick);
                base_tick_  = counter_.TicksSince(now);
                ratio_core_ = core;
                ratio_tick_ = tick;
            }
        }
        Arm(now);
    }

    void OnIdle() {
        if (!counting_) return;
        const FreescaleLowPowerMode mode = clocks_->WfiMode();
        uint32_t enable = 0u;
        switch (mode) {
            case FreescaleLowPowerMode::kRun:  return;
            case FreescaleLowPowerMode::kWait: enable = kCrWaiten; break;
            case FreescaleLowPowerMode::kDoze: enable = kCrDozen;  break;
            case FreescaleLowPowerMode::kStop:
            case FreescaleLowPowerMode::kStateRetention: enable = kCrStopen; break;
        }
        if ((cr_ & enable) != 0u && clocks_->InputRunsIn(kUnit, Input(), mode)) return;
        suspended_ = true;
        Retime();
    }

    void OnIdleExit() {
        if (!suspended_) return;
        suspended_ = false;
        Retime();
    }

    void WriteCr(uint32_t value) {
        LOG(SocTimer, "EPIT %08X: CR <- %08X (was %08X)\n", kBase, value, cr_);
        if ((value & kCrSwr) != 0u) {
            ResetRegisters(cr_ & kCrSwrKeep);
            return;
        }
        value &= kCrWritable;
        if (Om(value) != 0u) {
            emu_.Get<Fatal>().Die("EPIT %08X: EPITCR 0x%08X drives the ipp_do_epito "
                                  "output pin, which is not modeled", kBase, value);
        }
        const uint32_t old       = cr_;
        const bool     was_en    = (old & kCrEn) != 0u;
        const bool     now_en    = (value & kCrEn) != 0u;
        const bool     new_scale = Prescaler(old) != Prescaler(value);
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        const bool divided = counting_ && now_en && new_scale;
        CycleAnchoredCounter::Phase phase;
        if (divided) {
            phase = counter_.DivideChangePhase(now, uint64_t{Prescaler(old)} + 1u,
                                               uint64_t{Prescaler(value)} + 1u);
            Freeze(now);
        } else if (counting_) {
            if (now_en) Rebase(now);
            else        Freeze(now);
        }
        cr_ = value;
        /* MCIMX31RM §33.6.1.1 with Figure 33-9: "A change in the value of the PRESCALER
           field is immediately reflected on its output clock frequency." */
        if (new_scale) {
            if (divided) phase_ = phase;
            else         ResetPrescaler();
        }
        if (!was_en && now_en && (value & kCrEnmod) != 0u) {
            base_value_ = (value & kCrRld) != 0u ? lr_ : kCntReset;
            ResetPrescaler();
        }
        Retime();
        RefreshIrq();
    }

    void WriteSr(uint32_t value) {
        if ((value & kSrOcif) == 0u) return;
        ocif_pending_ = false;
        next_ocif_    = kNoOcif;
        sr_ &= ~kSrOcif;
        clock_->Disarm(event_);
        RefreshIrq();
        Arm(clock_->Cycles());
    }

    void WriteLr(uint32_t value) {
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        if (counting_) Rebase(now);
        lr_ = value;
        if ((cr_ & kCrIovw) != 0u) base_value_ = value;
        Arm(now);
    }

    void WriteCmpr(uint32_t value) {
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        if (counting_) {
            const uint32_t cnt = Count(now);
            const int32_t  gap = static_cast<int32_t>(cnt - value);
            if (gap <= 0) {
                LOG(Caution, "[EPITPAST] EPIT %08X cmpr<-%08X cnt=%08X behind=%d ticks\n",
                    kBase, value, cnt, -gap);
            }
        }
        cmpr_ = value;
        Arm(now);
    }

    void ResetRegisters(uint32_t cr) {
        clock_->Disarm(event_);
        ocif_pending_    = false;
        next_ocif_       = kNoOcif;
        counting_        = false;
        suspended_       = false;
        cr_              = cr;
        sr_              = 0u;
        lr_              = kLrReset;
        cmpr_            = 0u;
        base_value_      = kCntReset;
        ResetPrescaler();
        Retime();
        RefreshIrq();
    }

    void HardwareReset() { ResetRegisters(0u); }

    static constexpr uint64_t kNoOcif = UINT64_MAX;

    GuestCycleClock*        clock_  = nullptr;
    FreescaleTimerClocks*   clocks_ = nullptr;
    GuestCycleClock::Event* event_  = nullptr;
    CycleAnchoredCounter    counter_;

    uint32_t cr_   = 0u;
    uint32_t sr_   = 0u;
    uint32_t lr_   = kLrReset;
    uint32_t cmpr_ = 0u;

    bool     counting_        = false;
    bool     suspended_       = false;
    bool     ocif_pending_    = false;
    uint64_t next_ocif_       = kNoOcif;
    uint32_t base_value_      = kCntReset;
    uint64_t base_tick_       = 0u;
    CycleAnchoredCounter::Phase phase_;

    GuestCycleClock::Rate ratio_core_{0u, 1u};
    GuestCycleClock::Rate ratio_tick_{0u, 1u};
};

}

}
