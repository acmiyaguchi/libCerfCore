#include "imx28_apb_dma.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_icoll.h"

namespace cerfcore {

namespace {

/* Sections 6.5 and 7.5 memory maps. */
constexpr uint32_t kCtrl0       = 0x000u;
constexpr uint32_t kCtrl1       = 0x010u;
constexpr uint32_t kCtrl2       = 0x020u;
constexpr uint32_t kChannelCtrl = 0x030u;
constexpr uint32_t kDevsel      = 0x040u;
constexpr uint32_t kBurstSize   = 0x050u;
constexpr uint32_t kDebug       = 0x060u;
constexpr uint32_t kChanBase    = 0x100u;
constexpr uint32_t kChanStride  = 0x070u;
constexpr uint32_t kChanEnd     = kChanBase + kImx28ApbDmaChannels * kChanStride;
constexpr uint32_t kVersion     = 0x800u;
constexpr uint32_t kMapEnd      = 0x810u;

constexpr uint32_t kChCurCmd = 0x00u;
constexpr uint32_t kChNxtCmd = 0x10u;
constexpr uint32_t kChCmd    = 0x20u;
constexpr uint32_t kChBar    = 0x30u;
constexpr uint32_t kChSema   = 0x40u;
constexpr uint32_t kChDebug1 = 0x50u;
constexpr uint32_t kChDebug2 = 0x60u;

/* Sections 6.5.2, 7.5.2 CTRL1: IRQ_EN [31:16], IRQ [15:0]; sections 6.5.3, 7.5.3 CTRL2:
   ERROR_STATUS [31:16] (R, no W access), ERROR_IRQ [15:0]. */
constexpr uint32_t kIrqMask      = 0xFFFFu;
constexpr uint32_t kIrqEnShift   = 16u;
constexpr uint32_t kErrorStatusMask = 0xFFFF0000u;

/* Sections 6.5.4, 7.5.4 CHANNEL_CTRL: RESET_CHANNEL [31:16], FREEZE_CHANNEL [15:0]. */
constexpr uint32_t kFreezeMask   = 0xFFFFu;
constexpr uint32_t kResetShift   = 16u;

/* Section 6.5.6 DMA_BURST_SIZE reset 0055_5555h; section 6.5.7 DEBUG reset 0. */
constexpr uint32_t kBurstReset   = 0x00555555u;

/* Sections 6.5.12, 7.5.10 SEMA: PHORE [23:16], INCREMENT_SEMA [7:0]. */
constexpr uint32_t kPhoreShift   = 16u;
constexpr uint32_t kIncrementMask = 0xFFu;

/* Section 39.3: _SET, _CLR and _TOG exist for CTRL0, CTRL1, CTRL2 and CHANNEL_CTRL. */
bool HasSctAlias(uint32_t reg) {
    return reg == kCtrl0 || reg == kCtrl1 || reg == kCtrl2 || reg == kChannelCtrl;
}

}  /* namespace */

void Imx28ApbDma::Init() {
    icoll_ = &emu_.Get<Imx28Icoll>();
    Imx28ApbDmaArbiter& arbiter = *this;
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) {
        channels_[c] = std::make_unique<Imx28ApbDmaChannel>(emu_, Spec(), MmioBase(), c, arbiter);
    }
    LoadResetValues();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { LoadResetValues(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

Imx28ApbDmaChannel& Imx28ApbDma::Channel(uint32_t c) { return *channels_[c]; }

void Imx28ApbDma::AttachDevice(uint32_t channel, Imx28ApbDmaDevice* device) {
    if (channel >= kImx28ApbDmaChannels) {
        emu_.Get<Fatal>().Die("Imx28 %08X: device attached to channel %u", MmioBase(), channel);
    }
    Channel(channel).Attach(device);
}

void Imx28ApbDma::NotifyEndCommand(uint32_t channel) {
    if (channel >= kImx28ApbDmaChannels) {
        emu_.Get<Fatal>().Die("Imx28 %08X: end command on channel %u", MmioBase(), channel);
    }
    Channel(channel).EndCommand();
    DriveLines(false);
}

void Imx28ApbDma::NotifyTransferDone(uint32_t channel) {
    if (channel >= kImx28ApbDmaChannels) {
        emu_.Get<Fatal>().Die("Imx28 %08X: transfer done on channel %u", MmioBase(), channel);
    }
    Channel(channel).TransferDone();
    DriveLines(false);
}

/* Sections 6.5 and 7.5 register tables, Reset value column. */
void Imx28ApbDma::LoadResetValues() {
    const Imx28ApbDmaSpec& spec = Spec();
    ctrl0_  = spec.ctrl0_reset;
    ctrl1_  = 0u;
    ctrl2_  = 0u;
    freeze_ = 0u;
    burst_  = spec.has_burst_and_debug ? kBurstReset : 0u;
    debug_  = 0u;
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) Channel(c).Reset();
    DriveLines(false);
}

/* Section 6.5.4 FREEZE_CHANNEL, section 6.5.1 CLKGATE_CHANNEL, SFTRST and CLKGATE. */
bool Imx28ApbDma::ChannelGranted(uint32_t c) const {
    if (!Operating()) return false;
    if (((freeze_ >> c) & 1u) != 0u) return false;
    return ((ctrl0_ & Spec().clkgate_channel_mask) >> c & 1u) == 0u;
}

void Imx28ApbDma::CommandComplete(uint32_t c) { ctrl1_ |= 1u << c; }

/* Section 6.5.3 ERROR_STATUS 0 TERMINATION. */
void Imx28ApbDma::DeviceTerminated(uint32_t c) {
    ctrl2_ |= 1u << c;
    ctrl2_ &= ~(1u << (kIrqEnShift + c));
}

/* Section 6.5.2: interrupt request is IRQ AND IRQ_EN; section 6.5.3: ERROR_IRQ is ORed with it.
   Table 5-1 gives the ICOLL source of each channel. */
void Imx28ApbDma::DriveLines(bool force) {
    const Imx28ApbDmaSpec& spec = Spec();
    const uint32_t active = (ctrl1_ & (ctrl1_ >> kIrqEnShift) & kIrqMask) | (ctrl2_ & kIrqMask);
    uint32_t want[4] = {};
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) {
        if (((active >> c) & 1u) == 0u) continue;
        const uint32_t src = spec.channel[c].irq_source;
        if (src == kImx28ApbDmaNoSource) {
            emu_.Get<Fatal>().Die("Imx28 %08X: channel %u interrupt with no source (CTRL1 %08X "
                                  "CTRL2 %08X)", MmioBase(), c, ctrl1_, ctrl2_);
        }
        want[src >> 5] |= 1u << (src & 31u);
    }
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) {
        const uint32_t src = spec.channel[c].irq_source;
        if (src == kImx28ApbDmaNoSource) continue;
        const uint32_t bit  = 1u << (src & 31u);
        const bool     on   = (want[src >> 5] & bit) != 0u;
        const bool     held = (line_[src >> 5] & bit) != 0u;
        if (!force && on == held) continue;
        line_[src >> 5] = on ? (line_[src >> 5] | bit) : (line_[src >> 5] & ~bit);
        if (on) icoll_->AssertSource(src);
        else    icoll_->DeassertSource(src);
    }
}

void Imx28ApbDma::PostRestore() { DriveLines(true); }

void Imx28ApbDma::RunPending() {
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) Channel(c).Resume();
}

uint32_t Imx28ApbDma::ReadChannelRegister(uint32_t c, uint32_t reg, uint32_t addr) {
    const Imx28ApbDmaChannel& ch = Channel(c);
    switch (reg) {
        case kChCurCmd: return ch.Current();
        case kChNxtCmd: return ch.Next();
        case kChCmd:    return ch.Command();
        case kChBar:    return ch.Buffer();
        case kChSema:   return ch.Phore() << kPhoreShift;
        case kChDebug1: return ch.Debug1();
        case kChDebug2: return 0u;
        default:        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
}

uint32_t Imx28ApbDma::ReadLanes(uint32_t word_addr, uint32_t) {
    const uint32_t off = word_addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", word_addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", word_addr, 0);
        return 0u;
    }
    if (a.reg_offset == kCtrl0) return ctrl0_;
    RequireOperating("ReadWord", ctrl0_, word_addr, 0);
    switch (a.reg_offset) {
        case kCtrl1:       return ctrl1_;
        case kCtrl2:       return ctrl2_;
        case kChannelCtrl: return freeze_;
        case kDevsel:      return 0u;
        case kVersion:     return Spec().version;
        default:           break;
    }
    if (Spec().has_burst_and_debug) {
        if (a.reg_offset == kBurstSize) return burst_;
        if (a.reg_offset == kDebug)     return debug_;
    }
    if (a.reg_offset >= kChanBase && a.reg_offset < kChanEnd) {
        const uint32_t rel = a.reg_offset - kChanBase;
        return ReadChannelRegister(rel / kChanStride, rel % kChanStride, word_addr);
    }
    HaltUnsupportedAccess("ReadWord", word_addr, 0);
}

uint32_t Imx28ApbDma::ReadWord(uint32_t addr) { return ReadLanes(addr, 0xFFFFFFFFu); }

void Imx28ApbDma::WriteCtrl0(Imx28SctOp op, uint32_t value) {
    const uint32_t old  = ctrl0_;
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~Spec().ctrl0_writable) | (next & Spec().ctrl0_writable);
    if (IsSoftReset(old, next)) {
        LoadResetValues();
        return;
    }
    ctrl0_ = next;
    RunPending();
    DriveLines(false);
}

void Imx28ApbDma::WriteChannelCtrl(Imx28SctOp op, uint32_t value) {
    const uint32_t resets = Imx28ApplySct(op, 0u, value) >> kResetShift;
    freeze_ = Imx28ApplySct(op, freeze_, value) & kFreezeMask;
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) {
        if (((resets >> c) & 1u) != 0u) Channel(c).Reset();
    }
    RunPending();
    DriveLines(false);
}

void Imx28ApbDma::WriteBurstSize(uint32_t value) {
    if (value != kBurstReset) HaltUnsupportedAccess("WriteWord", MmioBase() + kBurstSize, value);
    burst_ = value;
}

void Imx28ApbDma::WriteDebug(uint32_t value) {
    if (value != 0u) HaltUnsupportedAccess("WriteWord", MmioBase() + kDebug, value);
    debug_ = value;
}

void Imx28ApbDma::WriteChannelRegister(uint32_t c, uint32_t reg, uint32_t addr, uint32_t value) {
    switch (reg) {
        case kChNxtCmd:
            Channel(c).SetNextCommand(value);
            return;
        case kChSema:
            Channel(c).IncrementSemaphore(value & kIncrementMask);
            DriveLines(false);
            return;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

void Imx28ApbDma::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl0) {
        WriteCtrl0(a.op, value);
        return;
    }
    RequireOperating("WriteWord", ctrl0_, addr, value);
    switch (a.reg_offset) {
        case kCtrl1:
            ctrl1_ = Imx28ApplySct(a.op, ctrl1_, value);
            DriveLines(false);
            return;
        case kCtrl2:
            ctrl2_ = (ctrl2_ & kErrorStatusMask) |
                     (Imx28ApplySct(a.op, ctrl2_, value) & ~kErrorStatusMask);
            DriveLines(false);
            return;
        case kChannelCtrl:
            WriteChannelCtrl(a.op, value);
            return;
        default:
            break;
    }
    if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("WriteWord", addr, value);
    if (Spec().has_burst_and_debug && a.reg_offset == kBurstSize) {
        WriteBurstSize(value);
        return;
    }
    if (Spec().has_burst_and_debug && a.reg_offset == kDebug) {
        WriteDebug(value);
        return;
    }
    if (a.reg_offset >= kChanBase && a.reg_offset < kChanEnd) {
        const uint32_t rel = a.reg_offset - kChanBase;
        WriteChannelRegister(rel / kChanStride, rel % kChanStride, addr, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28ApbDma::SaveState(StateWriter& w) {
    w.Write("ctrl0", ctrl0_);
    w.Write("ctrl1", ctrl1_);
    w.Write("ctrl2", ctrl2_);
    w.Write("freeze", freeze_);
    w.Write("burst", burst_);
    w.Write("debug", debug_);
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) Channel(c).Save(w);
}

void Imx28ApbDma::RestoreState(StateReader& r) {
    r.Read("ctrl0", ctrl0_);
    r.Read("ctrl1", ctrl1_);
    r.Read("ctrl2", ctrl2_);
    r.Read("freeze", freeze_);
    r.Read("burst", burst_);
    r.Read("debug", debug_);
    for (uint32_t c = 0; c < kImx28ApbDmaChannels; ++c) Channel(c).Restore(r);
}

}
