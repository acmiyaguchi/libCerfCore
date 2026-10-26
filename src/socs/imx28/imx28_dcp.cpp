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

/* MCIMX28RM Rev. 2 chapter 13 (DCP) at 0x8002_8000. */
class Imx28Dcp : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80028000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kChannels = 4u;

    void ResetBlock();
    void WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void WriteChannel(uint32_t ch, uint32_t reg, Imx28SctOp op, uint32_t value, uint32_t addr);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    uint32_t ctrl_        = 0u;
    uint32_t stat_        = 0u;
    uint32_t channelctrl_ = 0u;
    uint32_t context_     = 0u;
    uint32_t cmdptr_[kChannels] = {};
    uint32_t chstat_[kChannels] = {};
    uint32_t chopts_[kChannels] = {};
};

/* Section 13.3 memory map: CTRL 0x000, STAT 0x010, CHANNELCTRL 0x020, CAPABILITY0 0x030,
   CAPABILITY1 0x040, CONTEXT 0x050; CHnCMDPTR 0x100, CHnSEMA 0x110, CHnSTAT 0x120, CHnOPTS 0x130
   at 0x40 per channel. */
constexpr uint32_t kOffCtrl        = 0x000u;
constexpr uint32_t kOffStat        = 0x010u;
constexpr uint32_t kOffChannelCtrl = 0x020u;
constexpr uint32_t kOffCapability0 = 0x030u;
constexpr uint32_t kOffCapability1 = 0x040u;
constexpr uint32_t kOffContext     = 0x050u;
constexpr uint32_t kOffChannel0    = 0x100u;
constexpr uint32_t kChannelStride  = 0x040u;
constexpr uint32_t kChCmdPtr       = 0x00u;
constexpr uint32_t kChSema         = 0x10u;
constexpr uint32_t kChStat         = 0x20u;
constexpr uint32_t kChOpts         = 0x30u;

/* Section 13.3.1 CTRL: reset F080_0000h; PRESENT_CRYPTO [29], PRESENT_SHA [28] read-only;
   GATHER_RESIDUAL_WRITES [23], ENABLE_CONTEXT_CACHING [22], ENABLE_CONTEXT_SWITCHING [21],
   CHANNEL_INTERRUPT_ENABLE [7:0]. */
constexpr uint32_t kCtrlReset    = 0xF0800000u;
constexpr uint32_t kCtrlWritable = 0xC0E000FFu;
constexpr uint32_t kCtrlPresent  = 0x30000000u;

/* Section 13.3.2 STAT: reset 1000_0000h, OTP_KEY_READY [28], IRQ [3:0]. */
constexpr uint32_t kStatReset = 0x10000000u;
constexpr uint32_t kStatIrq   = 0x0000000Fu;

/* Section 13.3.3 CHANNELCTRL: CH0_IRQ_MERGED [16], HIGH_PRIORITY_CHANNEL [15:8],
   ENABLE_CHANNEL [7:0]. */
constexpr uint32_t kChannelCtrlWritable = 0x0001FFFFu;

/* Sections 13.3.4 CAPABILITY0 reset 0000_0404h, 13.3.5 CAPABILITY1 reset 0007_0001h. */
constexpr uint32_t kCapability0 = 0x00000404u;
constexpr uint32_t kCapability1 = 0x00070001u;

}  /* namespace */

bool Imx28Dcp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Dcp::OnReady() {
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Dcp::ResetBlock() {
    ctrl_        = kCtrlReset;
    stat_        = kStatReset;
    channelctrl_ = 0u;
    context_     = 0u;
    for (uint32_t ch = 0; ch < kChannels; ++ch) cmdptr_[ch] = chstat_[ch] = chopts_[ch] = 0u;
}

void Imx28Dcp::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 DCP: %s at %08X (value %08X, CTRL %08X)", what, addr, value,
                          ctrl_);
}

/* Section 13.3.1 SFTRST and CLKGATE; sections 5.3, 39.5.10 soft reset. */
void Imx28Dcp::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    if (op != Imx28SctOp::Clr && (value & ~(kCtrlWritable | kCtrlPresent)) != 0u) {
        Die("CTRL reserved bits", addr, value);
    }
    const uint32_t next = (Imx28ApplySct(op, old, value) & kCtrlWritable) | (old & ~kCtrlWritable);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        return;
    }
    ctrl_ = next;
}

/* Section 13.3.17 CHnSEMA: an increment starts the channel on the packet at CHnCMDPTR;
   sections 13.3.18, 13.3.19 CHnSTAT and CHnOPTS have SCT aliases. */
void Imx28Dcp::WriteChannel(uint32_t ch, uint32_t reg, Imx28SctOp op, uint32_t value,
                            uint32_t addr) {
    switch (reg) {
        case kChCmdPtr:
            if (op != Imx28SctOp::Write) HaltUnsupportedAccess("WriteWord", addr, value);
            cmdptr_[ch] = value;
            return;
        case kChSema:
            if (op != Imx28SctOp::Write) HaltUnsupportedAccess("WriteWord", addr, value);
            Die("channel semaphore increment, work packet processing", addr, value);
        case kChStat:
            chstat_[ch] = Imx28ApplySct(op, chstat_[ch], value);
            return;
        case kChOpts:
            chopts_[ch] = Imx28ApplySct(op, chopts_[ch], value);
            return;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

uint32_t Imx28Dcp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if ((off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) return 0u;
    if (a.reg_offset == kOffCtrl) return ctrl_;
    RequireOperating("ReadWord", ctrl_, addr, 0);
    switch (a.reg_offset) {
        case kOffStat:        return stat_;
        case kOffChannelCtrl: return channelctrl_;
        case kOffCapability0: return kCapability0;
        case kOffCapability1: return kCapability1;
        case kOffContext:     return context_;
        default:              break;
    }
    if (a.reg_offset >= kOffChannel0 && a.reg_offset < kOffChannel0 + kChannels * kChannelStride) {
        const uint32_t ch  = (a.reg_offset - kOffChannel0) / kChannelStride;
        const uint32_t reg = (a.reg_offset - kOffChannel0) % kChannelStride;
        if (reg == kChCmdPtr) return cmdptr_[ch];
        if (reg == kChSema) return 0u;
        if (reg == kChStat) return chstat_[ch];
        if (reg == kChOpts) return chopts_[ch];
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Dcp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if ((off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kOffCtrl) {
        WriteCtrl(a.op, value, addr);
        return;
    }
    RequireOperating("WriteWord", ctrl_, addr, value);
    switch (a.reg_offset) {
        case kOffStat:
            if (a.op != Imx28SctOp::Clr) Die("STAT write other than CLR", addr, value);
            stat_ &= ~(value & kStatIrq);
            return;
        case kOffChannelCtrl: {
            const uint32_t next = Imx28ApplySct(a.op, channelctrl_, value);
            if ((next & ~kChannelCtrlWritable) != 0u) Die("CHANNELCTRL reserved bits", addr, value);
            channelctrl_ = next;
            return;
        }
        case kOffContext:
            if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("WriteWord", addr, value);
            context_ = value;
            return;
        default:
            break;
    }
    if (a.reg_offset >= kOffChannel0 && a.reg_offset < kOffChannel0 + kChannels * kChannelStride) {
        WriteChannel((a.reg_offset - kOffChannel0) / kChannelStride,
                     (a.reg_offset - kOffChannel0) % kChannelStride, a.op, value, addr);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Dcp::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
    w.Write("stat", stat_);
    w.Write("channelctrl", channelctrl_);
    w.Write("context", context_);
    w.Write("cmdptr", cmdptr_);
    w.Write("chstat", chstat_);
    w.Write("chopts", chopts_);
}

void Imx28Dcp::RestoreState(StateReader& r) {
    r.Read("ctrl", ctrl_);
    r.Read("stat", stat_);
    r.Read("channelctrl", channelctrl_);
    r.Read("context", context_);
    r.Read("cmdptr", cmdptr_);
    r.Read("chstat", chstat_);
    r.Read("chopts", chopts_);
}

REGISTER_SERVICE(Imx28Dcp);

}
