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
#include "freescale_gpt_count.h"
#include "freescale_gpt_regs.h"
#include "freescale_timer_clocks.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace cerf_freescale_gpt_detail {

template <uint32_t kBase, const std::string_view& kSoc, typename Traits>
class FreescaleGptBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == kSoc;
    }

    void OnReady() override {
        clock_  = &emu_.Get<GuestCycleClock>();
        clocks_ = &emu_.Get<FreescaleTimerClocks>();
        event_  = clock_->Add([this] { OnEvent(); });
        clock_->RegisterRateListener([this] { Retime(); });
        clock_->RegisterIdleListener([this] { OnIdle(); });
        clock_->RegisterIdleExitListener([this] { OnIdleExit(); });
        clocks_->RegisterRateListener([this] { Retime(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetRegisters(0u);
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    FastReadFn  FastReader() override { return &FreescaleGptBase::FastReadThunk; }
    FastWriteFn FastWriter() override { return &FreescaleGptBase::FastWriteThunk; }

    uint32_t ReadWord(uint32_t addr) override { return FastRead(addr - kBase, 4u); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        FastWrite(addr - kBase, value, 4u);
    }

    void SaveState(StateWriter& w) override {
        const uint64_t now = clock_->Cycles();
        SettleStatus(now);
        w.Write<uint32_t>("gptcr", cr_);
        w.Write<uint32_t>("gptpr", pr_);
        w.Write<uint32_t>("gptsr", sr_);
        w.Write<uint32_t>("gptir", ir_);
        w.WriteBytes("gptocr", ocr_, sizeof(ocr_));
        w.Write<uint32_t>("count", Count(now));
        (counting_ ? counter_.TickPhaseAt(now) : phase_).Save(w, "prescale_tick_phase");
    }

    void RestoreState(StateReader& r) override {
        uint32_t cr = 0, pr = 0, sr = 0, ir = 0, count = 0;
        uint32_t ocr[3] = {};
        CycleAnchoredCounter::Phase phase;
        r.Read("gptcr", cr);
        r.Read("gptpr", pr);
        r.Read("gptsr", sr);
        r.Read("gptir", ir);
        r.ReadBytes("gptocr", ocr, sizeof(ocr));
        r.Read("count", count);
        phase.Restore(r, "prescale_tick_phase");
        clock_->Disarm(event_);
        counting_   = false;
        suspended_  = false;
        pending_sr_ = 0u;
        next_hit_   = kNoHit;
        cr_         = cr;
        pr_         = pr;
        sr_         = sr;
        ir_         = ir;
        for (int n = 0; n < 3; ++n) ocr_[n] = ocr[n];
        base_value_ = count;
        phase_      = phase;
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
        return static_cast<FreescaleGptBase*>(ctx)->FastRead(off, width);
    }
    static void FastWriteThunk(void* ctx, uint32_t off, uint32_t value, uint32_t width) {
        static_cast<FreescaleGptBase*>(ctx)->FastWrite(off, value, width);
    }

    static uint32_t Clksrc(uint32_t cr) { return (cr >> kGptcrClksrcSh) & 0x7u; }

    uint32_t FastRead(uint32_t off, uint32_t width) {
        if (width != 4u) HaltUnsupportedAccess("FastRead", kBase + off, 0);
        switch (off) {
            case kOffGptcr:   return cr_;
            case kOffGptpr:   return pr_;
            case kOffGptsr:   SettleStatus(clock_->Cycles()); return sr_;
            case kOffGptir:   return ir_;
            case kOffGptocr1: return ocr_[0];
            case kOffGptocr2: return ocr_[1];
            case kOffGptocr3: return ocr_[2];
            case kOffGpticr1:
            case kOffGpticr2: return 0u;
            case kOffGptcnt:  return ReadCount();
            default: break;
        }
        HaltUnsupportedAccess("FastRead", kBase + off, 0);
    }

    /* MCIMX31RM Table 34-4 / MCIMX51RM Table 36-4: writes to read-only bits have no
       effect. */
    void FastWrite(uint32_t off, uint32_t value, uint32_t width) {
        if (width != 4u) HaltUnsupportedAccess("FastWrite", kBase + off, value);
        switch (off) {
            case kOffGptcr:   WriteCr(value); return;
            case kOffGptpr:   WritePr(value); return;
            case kOffGptsr:   WriteSr(value); return;
            case kOffGptir:   WriteIr(value); return;
            case kOffGptocr1: WriteOcr(0, value); return;
            case kOffGptocr2: WriteOcr(1, value); return;
            case kOffGptocr3: WriteOcr(2, value); return;
            case kOffGpticr1:
            case kOffGpticr2:
            case kOffGptcnt:  return;
            default: break;
        }
        HaltUnsupportedAccess("FastWrite", kBase + off, value);
    }

    GptCountSequence Sequence() const {
        return GptCountSequence(base_value_, (cr_ & kGptcrFrr) != 0u, ocr_[0]);
    }

    uint64_t TicksAt(uint64_t now) const { return counter_.TicksSince(now) - base_tick_; }

    uint32_t Count(uint64_t now) const {
        return counting_ ? Sequence().ValueAt(TicksAt(now)) : base_value_;
    }

    uint32_t ReadCount() {
        return Count(clock_->Cycles());
    }

    /* MCIMX31RM 34.4.1.3 / MCIMX51RM 36.4.3: OFn sets when GPTOCRn matches GPTCNT;
       Table 34-8 / 36-7 ROV sets on the 0xFFFFFFFF to 0 rollover. */
    uint32_t HitsIn(uint64_t after, uint64_t upto) const {
        const GptCountSequence seq = Sequence();
        uint32_t bits = 0u;
        uint64_t at   = 0u;
        for (int n = 0; n < 3; ++n) {
            if (seq.NextCompare(ocr_[n], after, at) && at <= upto) bits |= kGptOf1 << n;
        }
        if (seq.NextRollover(after, at) && at <= upto) bits |= kGptRov;
        return bits;
    }

    bool NextEvent(uint64_t after, uint32_t sources, uint64_t& at) const {
        const GptCountSequence seq = Sequence();
        const uint32_t open = sources & ~(sr_ | pending_sr_);
        bool     found = false;
        uint64_t hit   = 0u;
        for (int n = 0; n < 3; ++n) {
            if ((open & (kGptOf1 << n)) == 0u || !seq.NextCompare(ocr_[n], after, hit)) continue;
            if (!found || hit < at) at = hit;
            found = true;
        }
        if ((open & kGptRov) != 0u && seq.NextRollover(after, hit)) {
            if (!found || hit < at) at = hit;
            found = true;
        }
        return found;
    }

    void CaptureDue(uint64_t now) {
        if (!counting_) return;
        const uint64_t t = TicksAt(now);
        if (now >= next_hit_) pending_sr_ |= HitsIn(last_eval_, t);
        last_eval_ = t;
    }

    void SettleStatus(uint64_t now) {
        CaptureDue(now);
        if ((pending_sr_ & ~ir_) != 0u) Arm(now);
    }

    void Arm(uint64_t now) {
        uint64_t at = 0u;
        sr_         |= pending_sr_ & ~ir_;
        pending_sr_ &= ir_;
        next_hit_ = counting_ && NextEvent(last_eval_, kGptStatusMask, at)
                        ? counter_.CycleOfTick(base_tick_ + at) : kNoHit;
        if (pending_sr_ != 0u)                                clock_->Arm(event_, now);
        else if (counting_ && NextEvent(last_eval_, ir_, at)) clock_->Arm(event_,
                                                                  counter_.CycleOfTick(base_tick_ + at));
        else                                                  clock_->Disarm(event_);
    }

    void OnEvent() {
        const uint64_t now = clock_->Cycles();
        sr_ |= pending_sr_;
        pending_sr_ = 0u;
        if (counting_) {
            const uint64_t t = TicksAt(now);
            sr_ |= HitsIn(last_eval_, t);
            last_eval_ = t;
        }
        RefreshIrq();
        Arm(now);
    }

    void RefreshIrq() {
        if ((sr_ & ir_ & kGptStatusMask) != 0u) AssertIrqLine();
        else                                    DeassertIrqLine();
    }

    FreescaleTimerInput Input() const {
        switch (Traits::Clksrc(Clksrc(cr_))) {
            case GptClockInput::kIpg:      return FreescaleTimerInput::kIpg;
            case GptClockInput::kHighfreq: return FreescaleTimerInput::kHighfreq;
            default:                       return FreescaleTimerInput::kLowfreq;
        }
    }

    GuestCycleClock::Rate EnabledInputRate() const {
        if (suspended_ || (cr_ & kGptcrEn) == 0u ||
            Traits::Clksrc(Clksrc(cr_)) == GptClockInput::kNone) {
            return GuestCycleClock::Rate{0u, 1u};
        }
        return clocks_->InputRate(FreescaleTimerUnit::kGpt, Input());
    }

    GuestCycleClock::Rate TickRate(GuestCycleClock::Rate input) const {
        return clocks_->PrescaledRate(FreescaleTimerUnit::kGpt, kBase, input, pr_);
    }

    void Rebase(uint64_t now) {
        base_value_ = Count(now);
        base_tick_  = counter_.TicksSince(now);
        last_eval_  = 0u;
    }

    void Freeze(uint64_t now, bool keep_phase) {
        base_value_ = Count(now);
        if (keep_phase) phase_ = counter_.TickPhaseAt(now);
        else            ResetPrescaler();
        counting_  = false;
        last_eval_ = 0u;
    }

    /* MCIMX51RM Table 36-6 and Figure 36-13, MCIMX31RM Figure 34-15: after a prescaler
       counter reset the next prescaled pulse comes one input clock later. */
    void ResetPrescaler() { phase_ = CounterPhase::LastInputClock(uint64_t{pr_} + 1u); }

    void Resume(uint64_t now, GuestCycleClock::Rate input) {
        const GuestCycleClock::Rate core = clock_->ClockRate();
        const GuestCycleClock::Rate tick = TickRate(input);
        counter_.SetRate(core, tick);
        counter_.AnchorAt(now, 0u, phase_);
        base_tick_  = 0u;
        last_eval_  = 0u;
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
            Freeze(now, true);
        } else {
            const GuestCycleClock::Rate core = clock_->ClockRate();
            const GuestCycleClock::Rate tick = TickRate(input);
            if (tick != ratio_tick_ || core != ratio_core_) {
                base_value_ = Count(now);
                counter_.RescaleRate(now, core, tick);
                base_tick_  = counter_.TicksSince(now);
                last_eval_  = 0u;
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
            case FreescaleLowPowerMode::kWait: enable = kGptcrWaiten; break;
            case FreescaleLowPowerMode::kDoze: enable = Traits::kDozeEnable; break;
            case FreescaleLowPowerMode::kStop:
            case FreescaleLowPowerMode::kStateRetention: enable = kGptcrStopen; break;
        }
        if ((cr_ & enable) != 0u &&
            clocks_->InputRunsIn(FreescaleTimerUnit::kGpt, Input(), mode)) {
            return;
        }
        suspended_ = true;
        Retime();
    }

    void OnIdleExit() {
        if (!suspended_) return;
        suspended_ = false;
        Retime();
    }

    void WriteCr(uint32_t value) {
        LOG(SocTimer, "GPT %08X: CR <- %08X (was %08X)\n", kBase, value, cr_);
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        if ((value & kGptcrSwr) != 0u) {
            ResetRegisters(cr_ & Traits::kCrSwrKeep);
            return;
        }
        if ((value & kGptcrPinModes) != 0u) {
            emu_.Get<Fatal>().Die("GPT %08X: GPTCR 0x%08X drives input capture, output "
                                  "compare or forced compare pins, which are not modeled",
                                  kBase, value);
        }
        const uint32_t      next  = value & kGptcrStored;
        const GptClockInput input = Traits::Clksrc(Clksrc(next));
        if (input == GptClockInput::kPad || input == GptClockInput::kUndefined) {
            emu_.Get<Fatal>().Die("GPT %08X: GPTCR 0x%08X selects CLKSRC %u (%s), which is not a "
                                  "modeled clock", kBase, value,
                                  Clksrc(next), input == GptClockInput::kPad
                                                    ? "the external pad clock"
                                                    : "an undefined code");
        }
        const bool was_en = (cr_ & kGptcrEn) != 0u;
        const bool now_en = (next & kGptcrEn) != 0u;
        if (counting_) {
            if (now_en) Rebase(now);
            else        Freeze(now, Traits::kPrescalerHoldsWhenDisabled);
        } else if (was_en && !now_en && !Traits::kPrescalerHoldsWhenDisabled) {
            ResetPrescaler();
        }
        cr_ = next;
        /* MCIMX31RM section 34.4.1 (page 34-20): disabled, "the counter freezes at its current value or is reset
           to 0 according to the setting of the ENMOD bit"; MCIMX51RM section 36.5 (page 36-19) step 7: "Set
           ENMOD=1 in GPTCR, to bring GPT counter to 0x00000000". */
        if (!now_en && (next & kGptcrEnmod) != 0u) base_value_ = 0u;
        if (!was_en && now_en && (next & kGptcrEnmod) != 0u) {
            base_value_ = 0u;
            ResetPrescaler();
        }
        Retime();
        RefreshIrq();
    }

    void WritePr(uint32_t value) {
        if ((value & kGptprMask) == pr_) return;
        const uint64_t now  = clock_->Cycles();
        const uint32_t next = value & kGptprMask;
        CaptureDue(now);
        CycleAnchoredCounter::Phase phase = CounterPhase::LastInputClock(uint64_t{next} + 1u);
        if (counting_) {
            phase = counter_.DivideChangePhase(now, uint64_t{pr_} + 1u, uint64_t{next} + 1u);
            Freeze(now, false);
        }
        pr_    = next;
        phase_ = phase;
        Retime();
    }

    void WriteSr(uint32_t value) {
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        const uint32_t clear = value & kGptStatusMask;
        sr_         &= ~clear;
        pending_sr_ &= ~clear;
        RefreshIrq();
        Arm(now);
    }

    void WriteIr(uint32_t value) {
        const uint64_t now = clock_->Cycles();
        CaptureDue(now);
        ir_ = value & kGptStatusMask;
        Arm(now);
        RefreshIrq();
    }

    void WriteOcr(int n, uint32_t value) {
        const uint64_t now     = clock_->Cycles();
        const bool     restart = n == 0 && (cr_ & kGptcrFrr) == 0u;
        CaptureDue(now);
        if (counting_) Rebase(now);
        if (restart) base_value_ = 0u;
        ocr_[n] = value;
        Arm(now);
    }

    void ResetRegisters(uint32_t cr) {
        clock_->Disarm(event_);
        counting_   = false;
        suspended_  = false;
        pending_sr_ = 0u;
        next_hit_   = kNoHit;
        cr_         = cr;
        pr_         = 0u;
        sr_         = 0u;
        ir_         = 0u;
        for (int n = 0; n < 3; ++n) ocr_[n] = kOcrReset;
        base_value_ = 0u;
        ResetPrescaler();
        Retime();
        RefreshIrq();
    }

    static constexpr uint64_t kNoHit = UINT64_MAX;

    GuestCycleClock*        clock_  = nullptr;
    FreescaleTimerClocks*   clocks_ = nullptr;
    GuestCycleClock::Event* event_  = nullptr;
    CycleAnchoredCounter    counter_;

    uint32_t cr_     = 0u;
    uint32_t pr_     = 0u;
    uint32_t sr_     = 0u;
    uint32_t ir_     = 0u;
    uint32_t ocr_[3] = {kOcrReset, kOcrReset, kOcrReset};

    bool     counting_     = false;
    bool     suspended_    = false;
    uint32_t pending_sr_   = 0u;
    uint64_t next_hit_     = kNoHit;
    uint32_t base_value_   = 0u;
    uint64_t base_tick_    = 0u;
    uint64_t last_eval_    = 0u;
    CycleAnchoredCounter::Phase phase_;

    GuestCycleClock::Rate ratio_core_{0u, 1u};
    GuestCycleClock::Rate ratio_tick_{0u, 1u};
};

}

}
