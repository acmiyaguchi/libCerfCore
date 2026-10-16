#define NOMINMAX

#include "omap3530_sdma_base.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "omap3530_dma_element_bus.h"

#include <cstdint>
#include <mutex>

namespace {

constexpr uint32_t kCcrOff       = 0x00u;
constexpr uint32_t kClnkCtrlOff  = 0x04u;
constexpr uint32_t kCicrOff      = 0x08u;
constexpr uint32_t kCsrOff       = 0x0Cu;
constexpr uint32_t kCsdpOff      = 0x10u;
constexpr uint32_t kCenOff       = 0x14u;
constexpr uint32_t kCfnOff       = 0x18u;
constexpr uint32_t kCssaOff      = 0x1Cu;
constexpr uint32_t kCdsaOff      = 0x20u;
constexpr uint32_t kCseiOff      = 0x24u;
constexpr uint32_t kCsfiOff      = 0x28u;
constexpr uint32_t kCdeiOff      = 0x2Cu;
constexpr uint32_t kCdfiOff      = 0x30u;
constexpr uint32_t kCsacOff      = 0x34u;
constexpr uint32_t kCdacOff      = 0x38u;
constexpr uint32_t kCcenOff      = 0x3Cu;
constexpr uint32_t kCcfnOff      = 0x40u;
constexpr uint32_t kColorOff     = 0x44u;

constexpr uint32_t kChannelStride = 0x60u;
constexpr uint32_t kChannelBase   = 0x80u;

constexpr uint32_t kCcrEnable            = 1u << 7;
constexpr uint32_t kCcrFs                = 1u << 5;
constexpr uint32_t kCcrBs                = 1u << 18;
constexpr uint32_t kCcrConstFillEnable   = 1u << 16;
constexpr uint32_t kCcrTransparentCopyEn = 1u << 17;
constexpr uint32_t kCcrSelSrcDstSync     = 1u << 24;

constexpr uint32_t kCsdpSrcEndianBig = 1u << 21;
constexpr uint32_t kCsdpDstEndianBig = 1u << 19;

/* OMAP35x TRM SPRUF98Y Table 9-39 DMA4_CSRi (pages 1036-1037): HALF 2, FRAME 3, LAST 4, BLOCK 5, SYNC 6
   "servicing a synchronized DMA request", PKT 7; Table 9-8 (page 1002): start of last frame = "The first
   element of the last frame transferred", half of frame = "Half of the current frame transferred". */
constexpr uint32_t kCsrHalf     = 1u << 2;
constexpr uint32_t kCsrFrame    = 1u << 3;
constexpr uint32_t kCsrLast     = 1u << 4;
constexpr uint32_t kCsrBlock    = 1u << 5;
constexpr uint32_t kCsrSync     = 1u << 6;
constexpr uint32_t kCsrPkt      = 1u << 7;
constexpr uint32_t kCsrMisAlign = 1u << 11;

constexpr uint32_t kClnkEnable  = 1u << 15;

constexpr uint32_t kOcpSoftReset = 1u << 1;
/* SPRUF98Y the "0x-" channel fields of Table 9-33 (page 1031, page 1032), Table 9-35 (page 1034), Table 9-37 (page 1034,
   page 1035), Table 9-41 (page 1038); Table 9-37 bits 10 and 9 reset 1; Table 9-31 (page 1029, page 1030) DMA4_GCR. */
constexpr uint32_t kCcrMemory  = 0x0306F020u;
constexpr uint32_t kClnkMemory = 0x1Fu;
constexpr uint32_t kCicrMemory = 0xE9BCu;
constexpr uint32_t kCsdpMemory = 0x003F3FFFu;
constexpr uint32_t kCicrReset  = 0x600u;
constexpr uint32_t kGcrReset   = 0x00010010u;

constexpr uint32_t kRevision = 0x00000040u;
constexpr uint32_t kCaps0    = 0x000C0000u;
constexpr uint32_t kCaps2    = 0x000001FFu;
constexpr uint32_t kCaps3    = 0x000000F3u;
constexpr uint32_t kCaps4    = 0x00001FFDu;

uint32_t ExtractSyncSource(uint32_t ccr) {
    return (ccr & 0x1Fu) | ((ccr & 0x00180000u) >> 14);
}

uint32_t ElementSize(uint32_t csdp) {
    return 1u << (csdp & 0x3u);
}

bool IsAligned(uint32_t addr, uint32_t es) {
    return (addr & (es - 1u)) == 0u;
}

}  /* namespace */

void Omap3530SdmaBase::OnReady() {
    channels_.resize(ChannelCount());
    bus_ = &emu_.Get<Omap3530DmaElementBus>();
    ResetLocked(true);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        std::lock_guard<std::recursive_mutex> lk(state_mu_);
        ResetLocked(kind == ResetLineKind::Rtc);
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* SPRUF98Y section 9.3.2.2.2 (page 986): hardware and software resets initialize the logic, the global registers and the
   flip-flop channel bits; "all remaining per-channel registers are memory-based and, therefore, are not reset". */
void Omap3530SdmaBase::ResetLocked(bool power_on) {
    for (Channel& c : channels_) {
        if (power_on) c = Channel{};
        c.ccr       &= kCcrMemory;
        c.clnk_ctrl &= kClnkMemory;
        c.cicr       = (c.cicr & kCicrMemory) | kCicrReset;
        c.csr        = 0u;
        c.csdp      &= kCsdpMemory;
        c.active     = false;
        c.stopped    = false;
    }
    ResetIrqRegisters();
    sysstatus_     = 1u;
    ocp_sysconfig_ = 0u;
    gcr_           = kGcrReset;
    chain_starts_.clear();
    UpdateIrqLines();
}

uint32_t Omap3530SdmaBase::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("ReadWord (misaligned)", addr, 0);

    std::lock_guard<std::recursive_mutex> lk(state_mu_);

    if (off < kChannelBase) {
        if (off == 0x000u) return kRevision;
        if (IsIrqRegister(off)) return ReadIrqRegister(off);
        if (off == 0x028u) return sysstatus_;
        if (off == 0x02Cu) return ocp_sysconfig_;
        if (off == 0x064u) return kCaps0;
        if (off == 0x06Cu) return kCaps2;
        if (off == 0x070u) return kCaps3;
        if (off == 0x074u) return kCaps4;
        if (off == 0x078u) return gcr_;
        return 0u;
    }

    const uint32_t rel = off - kChannelBase;
    const uint32_t count = static_cast<uint32_t>(channels_.size());
    if (rel >= count * kChannelStride) return 0u;
    const int ch = static_cast<int>(rel / kChannelStride);
    const uint32_t sub = rel % kChannelStride;
    const Channel& c = channels_[ch];
    switch (sub) {
    case kCcrOff:      return c.ccr;
    case kClnkCtrlOff: return c.clnk_ctrl;
    case kCicrOff:     return c.cicr;
    case kCsrOff:      return c.csr;
    case kCsdpOff:     return c.csdp;
    case kCenOff:      return c.cen;
    case kCfnOff:      return c.cfn;
    case kCssaOff:     return c.cssa;
    case kCdsaOff:     return c.cdsa;
    case kCseiOff:     return c.csei;
    case kCsfiOff:     return c.csfi;
    case kCdeiOff:     return c.cdei;
    case kCdfiOff:     return c.cdfi;
    case kCsacOff:     return c.csac;
    case kCdacOff:     return c.cdac;
    case kCcenOff:     return c.ccen;
    case kCcfnOff:     return c.ccfn;
    case kColorOff:    return c.color;
    default:           return 0u;
    }
}

void Omap3530SdmaBase::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("WriteWord (misaligned)", addr, value);

    std::lock_guard<std::recursive_mutex> lk(state_mu_);

    if (off < kChannelBase) {
        if (off == 0x000u) return;
        if (IsIrqRegister(off)) {
            WriteIrqRegister(off, value);
            return;
        }
        if (off == 0x028u) return;
        if (off == 0x02Cu) {
            if (value & kOcpSoftReset) ResetLocked(false);
            /* SPRUF98Y Table 9-21 (page 1024): SOFTRESET "During reads, it always returns 0." */
            ocp_sysconfig_ = value & ~kOcpSoftReset;
            return;
        }
        if (off == 0x064u || off == 0x06Cu ||
            off == 0x070u || off == 0x074u) return;
        if (off == 0x078u) { gcr_ = value; return; }
        return;
    }

    const uint32_t rel = off - kChannelBase;
    const uint32_t count = static_cast<uint32_t>(channels_.size());
    if (rel >= count * kChannelStride) return;
    const int ch = static_cast<int>(rel / kChannelStride);
    const uint32_t sub = rel % kChannelStride;
    Channel& c = channels_[ch];

    switch (sub) {
    case kCcrOff: {
        const bool was_enabled = (c.ccr & kCcrEnable) != 0u;
        const bool now_enabled = (value & kCcrEnable) != 0u;
        c.ccr = value;
        if (!was_enabled && now_enabled) {
            c.active  = false;
            c.stopped = false;
            c.ccen = 0;
            c.ccfn = 0;
            if (ExtractSyncSource(value) == 0u) {
                RunSwTransfer(ch);
                UpdateIrqLines();
            } else {
                ServeIfRequested(ch);
            }
            ServeChainStarts();
        }
        return;
    }
    case kClnkCtrlOff: c.clnk_ctrl = value; return;
    case kCicrOff:
        c.cicr = value;
        UpdateIrqLines();
        return;
    case kCsrOff:
        /* W1C: write-1-to-clear. */
        c.csr &= ~value;
        UpdateIrqLines();
        return;
    case kCsdpOff:  c.csdp  = value; return;
    case kCenOff:   c.cen   = value & 0x00FFFFFFu; return;
    case kCfnOff:   c.cfn   = value & 0x0000FFFFu; return;
    case kCssaOff:  c.cssa  = value; return;
    case kCdsaOff:  c.cdsa  = value; return;
    case kCseiOff:  c.csei  = value & 0x0000FFFFu; return;
    case kCsfiOff:  c.csfi  = value; return;
    case kCdeiOff:  c.cdei  = value & 0x0000FFFFu; return;
    case kCdfiOff:  c.cdfi  = value; return;
    case kCsacOff:  c.csac  = value; return;
    case kCdacOff:  c.cdac  = value; return;
    case kCcenOff:  c.ccen  = value & 0x00FFFFFFu; return;
    case kCcfnOff:  c.ccfn  = value & 0x0000FFFFu; return;
    case kColorOff: c.color = value; return;
    default: return;
    }
}

void Omap3530SdmaBase::RaiseSyncEvent(uint32_t source) {
    if (source == 0u) return;
    std::lock_guard<std::recursive_mutex> lk(state_mu_);
    uint32_t eligible = 0u;
    for (uint32_t ch = 0; ch < static_cast<uint32_t>(channels_.size()); ++ch) {
        const Channel& c = channels_[ch];
        if ((c.ccr & kCcrEnable) != 0u && !c.stopped && ExtractSyncSource(c.ccr) == source) eligible |= 1u << ch;
    }
    uint32_t served = 0;
    for (int ch = 0; eligible != 0u; ++ch, eligible >>= 1) {
        if ((eligible & 1u) != 0u) served += ExecuteSyncUnit(ch);
    }
    UpdateIrqLines();
    if (served != 0u) OnSyncServed(source);
    ServeChainStarts();
}

/* OMAP35x TRM SPRUF98Y section 9.4.1 (page 991): a hardware-synchronized logical channel becomes
   active "when the logical channel is enabled and the hardware DMA request line is asserted". */
void Omap3530SdmaBase::ServeIfRequested(int ch) {
    const uint32_t source = ExtractSyncSource(channels_[ch].ccr);
    if (!SyncRequestAsserted(source)) return;
    const uint32_t served = ExecuteSyncUnit(ch);
    UpdateIrqLines();
    if (served != 0u) OnSyncServed(source);
}

void Omap3530SdmaBase::ServeChainStarts() {
    while (!chain_starts_.empty()) {
        const int ch = chain_starts_.front();
        chain_starts_.erase(chain_starts_.begin());
        const Channel& c = channels_[ch];
        if ((c.ccr & kCcrEnable) == 0u || c.stopped || ExtractSyncSource(c.ccr) == 0u) continue;
        ServeIfRequested(ch);
    }
}

/* SPRUF98Y section 9.3.3.1 (page 986): "Each status bit is updated in the SDMA.DMA4_CSRi register only when the
   corresponding enable bit is enabled in the SDMA.DMA4_CICRi register"; Table 9-37 (page 1034): CICR bit 6 RESERVED. */
void Omap3530SdmaBase::SetStatus(Channel& c, uint32_t events) {
    c.csr |= events & (c.cicr | kCsrSync);
}

void Omap3530SdmaBase::RunSwTransfer(int ch) {
    Channel& c = channels_[ch];
    const uint32_t cen = c.cen & 0x00FFFFFFu;
    const uint32_t cfn = c.cfn & 0x0000FFFFu;
    if (cen == 0u || cfn == 0u) {
        SetStatus(c, kCsrBlock | kCsrFrame | kCsrLast);
        OnChannelComplete(ch);
        return;
    }
    c.csac = c.cssa;
    c.cdac = c.cdsa;
    c.ccen = 0;
    c.ccfn = 0;
    c.active = true;
    while (c.active) {
        if (!TransferOneElement(ch)) return;
    }
}

uint32_t Omap3530SdmaBase::ExecuteSyncUnit(int ch) {
    Channel& c = channels_[ch];
    if (!c.active) {
        c.csac = c.cssa;
        c.cdac = c.cdsa;
        c.ccen = 0;
        c.ccfn = 0;
        c.active = true;
    }
    const bool fs       = (c.ccr & kCcrFs) != 0u;
    const bool bs       = (c.ccr & kCcrBs) != 0u;
    const bool src_sync = (c.ccr & kCcrSelSrcDstSync) != 0u;

    uint32_t limit = 0xFFFFFFFFu;
    if (!fs && !bs) {
        limit = 1u;
    } else if (fs && !bs) {
        limit = c.cen & 0x00FFFFFFu;
    } else if (fs && bs) {
        limit = src_sync ? (c.csfi & 0xFFFFu) : (c.cdfi & 0xFFFFu);
    }
    SetStatus(c, kCsrSync);
    uint32_t moved = 0;
    while (moved < limit) {
        if (!TransferOneElement(ch)) return moved;
        ++moved;
        if (!c.active) break;
    }
    /* SPRUF98Y page 999 NOTE: "the packet interrupt is not generated in the packet source synchronized mode". */
    if (fs && bs && !src_sync) SetStatus(c, kCsrPkt);
    return moved;
}

bool Omap3530SdmaBase::TransferOneElement(int ch) {
    Channel& c = channels_[ch];
    const uint32_t es = ElementSize(c.csdp);
    if (es == 8u) {
        RaiseChannelFault(ch, kCsrMisAlign);
        return false;
    }
    const uint32_t src_amode = (c.ccr >> 12) & 0x3u;
    const uint32_t dst_amode = (c.ccr >> 14) & 0x3u;
    const bool const_fill   = (c.ccr & kCcrConstFillEnable)   != 0u;
    const bool transp_copy  = (c.ccr & kCcrTransparentCopyEn) != 0u;
    const bool src_endian   = (c.csdp & kCsdpSrcEndianBig)    != 0u;
    const bool dst_endian   = (c.csdp & kCsdpDstEndianBig)    != 0u;

    uint32_t src_val = 0;
    if (const_fill) {
        src_val = c.color;
    } else {
        const uint32_t spa = (src_amode == 0u) ? c.cssa : c.csac;
        if (!IsAligned(spa, es)) {
            RaiseChannelFault(ch, kCsrMisAlign);
            return false;
        }
        src_val = bus_->Read(spa, es, src_endian);
    }

    bool write_dst = true;
    if (transp_copy) {
        const uint32_t mask = cerf::ByteWidthMask(es);
        if ((src_val & mask) == (c.color & mask)) write_dst = false;
    }

    if (write_dst) {
        const uint32_t dpa = c.cdac;
        if (!IsAligned(dpa, es)) {
            RaiseChannelFault(ch, kCsrMisAlign);
            return false;
        }
        bus_->Write(dpa, es, dst_endian, src_val);
    }

    const uint32_t cen = c.cen & 0x00FFFFFFu;
    const uint32_t cfn = c.cfn & 0x0000FFFFu;
    c.ccen += 1u;
    if (c.ccen == 1u && c.ccfn + 1u == cfn) SetStatus(c, kCsrLast);
    const int32_t csei = static_cast<int32_t>(static_cast<int16_t>(
                            static_cast<uint16_t>(c.csei & 0xFFFFu)));
    const int32_t cdei = static_cast<int32_t>(static_cast<int16_t>(
                            static_cast<uint16_t>(c.cdei & 0xFFFFu)));
    const int32_t csfi = static_cast<int32_t>(c.csfi);
    const int32_t cdfi = static_cast<int32_t>(c.cdfi);

    const bool end_of_frame = (c.ccen == cen);
    if (!const_fill) c.csac = StepAddress(c.csac, src_amode, csei, csfi, es, end_of_frame);
    c.cdac = StepAddress(c.cdac, dst_amode, cdei, cdfi, es, end_of_frame);
    if (c.ccen == (cen + 1u) / 2u) SetStatus(c, kCsrHalf);

    if (end_of_frame) {
        c.ccen = 0;
        c.ccfn += 1u;
        SetStatus(c, kCsrFrame);
        if (c.ccfn == cfn) {
            SetStatus(c, kCsrBlock);
            OnChannelComplete(ch);
        }
    }
    return true;
}

uint32_t Omap3530SdmaBase::StepAddress(uint32_t cur, uint32_t amode,
                                       int32_t ei, int32_t fi,
                                       uint32_t es, bool end_of_frame) const {
    switch (amode) {
    case 0u: return cur;
    case 1u: return cur + es;
    case 2u: return cur + es + static_cast<uint32_t>(ei - 1);
    case 3u: return cur + es + static_cast<uint32_t>(
                              (end_of_frame ? fi : ei) - 1);
    default: return cur;
    }
}

/* SPRUF98Y section 9.4.3 (page 992): on a misaligned element "the transfer is stopped and an address error
   interrupt occurs, if enabled"; Table 9-8 (page 1002) lists the address error apart from the end of block. */
void Omap3530SdmaBase::RaiseChannelFault(int ch, uint32_t csr_fault_bits) {
    Channel& c = channels_[ch];
    SetStatus(c, csr_fault_bits);
    c.active  = false;
    c.stopped = true;
    LOG(Caution,
        "[SDMA] ch%u fault csr=0x%08X cssa=0x%08X cdsa=0x%08X csac=0x%08X "
        "cdac=0x%08X ccr=0x%08X csdp=0x%08X\n",
        static_cast<uint32_t>(ch), c.csr, c.cssa, c.cdsa, c.csac, c.cdac,
        c.ccr, c.csdp);
}

/* SPRUF98Y Table 9-10 (page 1019): DMA4_CCR2 reads 0x010c403e, ENABLE [7] 0, after a first transfer. */
void Omap3530SdmaBase::OnChannelComplete(int ch) {
    Channel& c = channels_[ch];
    c.active = false;
    c.ccr &= ~kCcrEnable;
    if ((c.clnk_ctrl & kClnkEnable) != 0u) {
        StartChain(static_cast<int>(c.clnk_ctrl & 0x1Fu));
    }
}

/* OMAP35x TRM SPRUF98Y section 9.5.6 (page 1011): "Each channel is enabled automatically in turn
   when the previous logical channel transfer completes"; Table 9-33 ENABLE (page 1032): "It is SW
   responsibility to clear the CSR register". */
void Omap3530SdmaBase::StartChain(int ch) {
    if (ch < 0 || ch >= static_cast<int>(channels_.size())) return;
    Channel& c = channels_[ch];
    c.ccr     |= kCcrEnable;
    c.active   = false;
    c.stopped  = false;
    c.ccen = 0;
    c.ccfn = 0;
    if (ExtractSyncSource(c.ccr) == 0u) {
        RunSwTransfer(ch);
    } else {
        chain_starts_.push_back(ch);
    }
}

uint32_t Omap3530SdmaBase::FiredChannels() const {
    uint32_t fired = 0u;
    for (size_t ch = 0; ch < channels_.size(); ++ch) {
        if ((channels_[ch].csr & channels_[ch].cicr) != 0u) fired |= 1u << ch;
    }
    return fired;
}

