#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_clkctrl.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_timrot_timer.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 chapter 23 (TIMROT) at 0x8006_8000. */
class Imx28Timrot : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80068000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    static constexpr uint32_t kTimers = 4u;

    bool              Operating() const { return !Gated(rotctrl_); }
    Imx28TimrotClocks Clocks() const;
    void              PushClocks();
    void              ResetBlock();
    void              WriteRotctrl(Imx28SctOp op, uint32_t value);

    Imx28Clkctrl*   clkctrl_ = nullptr;
    Imx28TimrotTimer timers_[kTimers] = {
        {emu_, 0u}, {emu_, 1u}, {emu_, 2u}, {emu_, 3u}};
    uint32_t rotctrl_ = 0u;
};

/* Section 23.4 memory map: ROTCTRL 0x00, ROTCOUNT 0x10, then per timer TIMCTRL, RUNNING_COUNT,
   FIXED_COUNT, MATCH_COUNT at 0x20 + 0x40 * n; VERSION 0x120. */
constexpr uint32_t kOffRotctrl   = 0x000u;
constexpr uint32_t kOffRotcount  = 0x010u;
constexpr uint32_t kOffTimer0    = 0x020u;
constexpr uint32_t kTimerStride  = 0x040u;
constexpr uint32_t kOffVersion   = 0x120u;
constexpr uint32_t kOffTimCtrl   = 0x00u;
constexpr uint32_t kOffRunning   = 0x10u;
constexpr uint32_t kOffFixed     = 0x20u;
constexpr uint32_t kOffMatch     = 0x30u;
constexpr uint32_t kMapEnd       = 0x130u;

/* Section 23.4.1 ROTCTRL: reset FE000000h; the present bits [29:25] are read-only, STATE
   [24:22] is a read-only view, DIVIDER [21:16], RELATIVE [12], OVERSAMPLE [11:10],
   POLARITY_B [9], POLARITY_A [8], SELECT_B [7:4], SELECT_A [3:0]. */
constexpr uint32_t kRotctrlReset    = 0xFE000000u;
constexpr uint32_t kRotctrlWritable = 0xC03F1FFFu;
constexpr uint32_t kRotctrlSelects  = 0xFFu;

/* Section 23.4.2 ROTCOUNT reset 0; section 23.4.19 VERSION reset 0200_0000h. */
constexpr uint32_t kVersion = 0x02000000u;

}  /* namespace */

bool Imx28Timrot::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Timrot::OnReady() {
    clkctrl_ = &emu_.Get<Imx28Clkctrl>();
    auto* icoll = &emu_.Get<Imx28Icoll>();
    for (Imx28TimrotTimer& t : timers_) t.Init(icoll);
    rotctrl_ = kRotctrlReset;
    for (Imx28TimrotTimer& t : timers_) t.Reset();
    clkctrl_->RegisterApbxListener([this] { PushClocks(); });
    emu_.Get<GuestCycleClock>().RegisterRateListener([this] { PushClocks(); });
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    reset.RegisterResetReleaseListener([this] { PushClocks(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

Imx28TimrotClocks Imx28Timrot::Clocks() const {
    Imx28TimrotClocks c;
    c.operating = Operating();
    c.xbus      = clkctrl_->XbusRate();
    return c;
}

void Imx28Timrot::PushClocks() {
    const Imx28TimrotClocks c = Clocks();
    for (Imx28TimrotTimer& t : timers_) t.Retime(c);
}

void Imx28Timrot::ResetBlock() {
    rotctrl_ = kRotctrlReset;
    for (Imx28TimrotTimer& t : timers_) t.Reset();
}

/* Section 23.4.1 SELECT_A/SELECT_B 0x1-0xA name the PWM and rotary pins. */
void Imx28Timrot::WriteRotctrl(Imx28SctOp op, uint32_t value) {
    const uint32_t old  = rotctrl_;
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~kRotctrlWritable) | (next & kRotctrlWritable);
    if ((next & kRotctrlSelects) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Timrot: ROTCTRL %08X selects a rotary input source", next);
    }
    if (IsSoftReset(old, next)) {
        ResetBlock();
        PushClocks();
        return;
    }
    rotctrl_ = next;
    PushClocks();
}

uint32_t Imx28Timrot::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t       reg = a.reg_offset;
    const bool timctrl = reg >= kOffTimer0 && reg < kOffVersion &&
                         (reg - kOffTimer0) % kTimerStride == kOffTimCtrl;
    if (a.op != Imx28SctOp::Write) {
        if (reg != kOffRotctrl && !timctrl) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    if (reg == kOffRotctrl)  return rotctrl_;
    if (reg == kOffRotcount) return 0u;
    if (reg == kOffVersion)  return kVersion;
    if (reg >= kOffTimer0 && reg < kOffVersion) {
        Imx28TimrotTimer& t = timers_[(reg - kOffTimer0) / kTimerStride];
        switch ((reg - kOffTimer0) % kTimerStride) {
            case kOffTimCtrl: return t.Ctrl();
            case kOffRunning: return t.Running();
            case kOffFixed:   return t.Fixed();
            case kOffMatch:   return t.Match();
            default:          break;
        }
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Timrot::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a   = Imx28DecodeSct(off);
    const uint32_t       reg = a.reg_offset;
    if (reg == kOffRotctrl) {
        WriteRotctrl(a.op, value);
        return;
    }
    RequireOperating("WriteWord", rotctrl_, addr, value);
    if (reg >= kOffTimer0 && reg < kOffVersion) {
        Imx28TimrotTimer& t = timers_[(reg - kOffTimer0) / kTimerStride];
        const uint32_t    r = (reg - kOffTimer0) % kTimerStride;
        if (r == kOffTimCtrl) {
            t.WriteCtrl(a.op, value);
            return;
        }
        if (a.op == Imx28SctOp::Write && r == kOffFixed) {
            t.WriteFixed(value);
            return;
        }
        if (a.op == Imx28SctOp::Write && r == kOffMatch) {
            t.WriteMatch(value);
            return;
        }
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Timrot::SaveState(StateWriter& w) {
    w.Write("rotctrl", rotctrl_);
    for (Imx28TimrotTimer& t : timers_) t.Save(w);
}

void Imx28Timrot::RestoreState(StateReader& r) {
    r.Read("rotctrl", rotctrl_);
    for (Imx28TimrotTimer& t : timers_) t.Restore(r);
}

void Imx28Timrot::PostRestore() {
    const Imx28TimrotClocks c = Clocks();
    for (Imx28TimrotTimer& t : timers_) t.PostRestore(c);
}

REGISTER_SERVICE(Imx28Timrot);

}
