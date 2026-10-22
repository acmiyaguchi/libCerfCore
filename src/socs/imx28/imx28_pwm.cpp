#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 chapter 28 (PWM) at 0x8006_4000. */
class Imx28Pwm : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80064000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kChannels = 8u;

    void ResetBlock();
    void WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void WritePeriod(uint32_t ch, Imx28SctOp op, uint32_t value, uint32_t addr);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    uint32_t ctrl_ = 0u;
    uint32_t active_[kChannels] = {};
    uint32_t period_[kChannels] = {};
};

/* Section 28.4 memory map: CTRL 0x000, ACTIVEn 0x010 + 0x20 * n, PERIODn 0x020 + 0x20 * n,
   VERSION 0x110. */
constexpr uint32_t kOffCtrl       = 0x000u;
constexpr uint32_t kOffActive0    = 0x010u;
constexpr uint32_t kChannelStride = 0x020u;
constexpr uint32_t kOffVersion    = 0x110u;
constexpr uint32_t kMapEnd        = 0x120u;

/* Section 28.4.1 CTRL: reset FFC0_0000h; PWMn_PRESENT [29:22] read-only, RSRVD1 [21:10],
   OUTPUT_CUTOFF_EN [9], RSRVD2 [8], PWMn_ENABLE [7:0]. */
constexpr uint32_t kCtrlReset    = 0xFFC00000u;
constexpr uint32_t kCtrlWritable = 0xC00002FFu;

/* Section 28.4.3 PERIODn: RSRVD2 [31:27], HSADC_OUT [26], HSADC_CLK_SEL [25], MATT [23]. */
constexpr uint32_t kPeriodReserved = 0xF8000000u;
constexpr uint32_t kPeriodHsadc    = 0x06000000u;
constexpr uint32_t kPeriodMatt     = 0x00800000u;

/* Section 28.4.18 VERSION reset 0106_0000h. */
constexpr uint32_t kVersion = 0x01060000u;

}  /* namespace */

bool Imx28Pwm::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Pwm::OnReady() {
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Pwm::ResetBlock() {
    ctrl_ = kCtrlReset;
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        active_[ch] = 0u;
        period_[ch] = 0u;
    }
}

void Imx28Pwm::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 PWM: %s at %08X (value %08X, CTRL %08X)", what, addr, value,
                          ctrl_);
}

/* Section 28.3; section 28.4.1. */
void Imx28Pwm::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        return;
    }
    if ((next & ~kCtrlWritable) != (kCtrlReset & ~kCtrlWritable)) {
        Die("CTRL read-only or reserved bits", addr, value);
    }
    ctrl_ = next;
}

void Imx28Pwm::WritePeriod(uint32_t ch, Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, period_[ch], value);
    if ((next & kPeriodReserved) != 0u) Die("PERIOD RSRVD2 bits", addr, value);
    if ((next & kPeriodHsadc) != 0u) Die("PERIOD HSADC_OUT or HSADC_CLK_SEL", addr, value);
    if ((next & kPeriodMatt) != 0u) Die("PERIOD MATT", addr, value);
    period_[ch] = next;
}

uint32_t Imx28Pwm::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (a.reg_offset == kOffCtrl) return ctrl_;
    if (a.reg_offset == kOffVersion) return kVersion;
    const uint32_t rel = a.reg_offset - kOffActive0;
    const uint32_t ch  = rel / kChannelStride;
    return (rel % kChannelStride) == 0u ? active_[ch] : period_[ch];
}

void Imx28Pwm::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kOffCtrl) {
        WriteCtrl(a.op, value, addr);
        return;
    }
    if (a.reg_offset == kOffVersion) Die("VERSION write", addr, value);
    /* Sections 28.2.5, 28.3. */
    if ((ctrl_ & kCtrlSftrst) != 0u) Die("channel register write with SFTRST set", addr, value);
    const uint32_t rel = a.reg_offset - kOffActive0;
    const uint32_t ch  = rel / kChannelStride;
    if ((rel % kChannelStride) == 0u) {
        active_[ch] = Imx28ApplySct(a.op, active_[ch], value);
        return;
    }
    WritePeriod(ch, a.op, value, addr);
}

void Imx28Pwm::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
    w.Write("active", active_);
    w.Write("period", period_);
}

void Imx28Pwm::RestoreState(StateReader& r) {
    r.Read("ctrl", ctrl_);
    r.Read("active", active_);
    r.Read("period", period_);
}

REGISTER_SERVICE(Imx28Pwm);

}
