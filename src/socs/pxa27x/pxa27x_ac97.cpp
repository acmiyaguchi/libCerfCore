#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/ac97_codec.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../pxa2xx/pxa2xx_ac97_bit_clock.h"
#include "../pxa2xx/pxa2xx_ac97_interrupt.h"
#include "../pxa2xx/pxa2xx_ac97_link.h"
#include "../pxa2xx/pxa2xx_ac97_mic_in.h"
#include "../pxa2xx/pxa2xx_ac97_modem.h"
#include "../pxa2xx/pxa2xx_ac97_modem_out.h"
#include "../pxa2xx/pxa2xx_ac97_pcm.h"
#include "../pxa2xx/pxa2xx_ac97_pcm_in.h"
#include "../pxa2xx/pxa2xx_dma.h"
#include "pxa270_id.h"
#include "pxa27x_clock_manager.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Intel PXA27x Developer's Manual Table 13-10 (page 13-26) POCR, Table 13-11 PCMICR, Table 13-16
   MCCR, Table 13-19 MOCR, Table 13-20 MICR: FEIE bit 3, FSRIE bit 1. */
constexpr uint32_t kFeie = 1u << 3, kFsrie = 1u << 1;
constexpr uint32_t kCtrlIrqEnables = kFeie | kFsrie;
/* Intel PXA27x Developer's Manual Table 13-17 (page 13-33) MCSR and Table 13-22 (page 13-38) MISR:
   FIFOE 4, EOC 3, FSR 2; Table 13-21 (page 13-37) MOSR: FIFOE 4, FSR 2. */
constexpr uint32_t kFifoe = 1u << 4, kEoc = 1u << 3, kFsr = 1u << 2;
/* Intel PXA27x Developer's Manual Table 13-8 (pages 13-21, 13-22) GCR. */
constexpr uint32_t kGcrNdmaen = 1u << 24;
constexpr uint32_t kGcrCdoneIe = 1u << 19, kGcrSdoneIe = 1u << 18, kGcrPrdyIe = 1u << 8, kGcrPresIe = 1u << 4;
constexpr uint32_t kGcrGpiIe = 1u << 0;
constexpr uint32_t kGcrIrqEnables = kGcrCdoneIe | kGcrSdoneIe | (1u << 9) | kGcrPrdyIe | (1u << 5) | kGcrPresIe |
                                    kGcrGpiIe;
constexpr uint32_t kGcrAcoff = 1u << 3, kGcrWrst = 1u << 2, kGcrNcrst = 1u << 1;
/* Intel PXA27x Developer's Manual Table 13-9 (pages 13-23 to 13-25) GSR. */
constexpr uint32_t kGsrCdone = 1u << 19, kGsrSdone = 1u << 18, kGsrRcs = 1u << 15, kGsrPresint = 1u << 10;
constexpr uint32_t kGsrPcrdy = 1u << 8, kGsrMcint = 1u << 7;
constexpr uint32_t kGsrPoint = 1u << 6, kGsrPiint = 1u << 5;
constexpr uint32_t kGsrAcoffd = 1u << 3, kGsrMoint = 1u << 2, kGsrMiint = 1u << 1, kGsrGsci = 1u << 0;
constexpr uint32_t kCaip = 1u << 0;
/* Intel PXA27x Developer's Manual section 13.6.3 (page 13-18): "the second read operation returns a
   timed-out data value of 0x0000_FFFF". */
constexpr uint32_t kReadTimeoutValue = 0x0000FFFFu;
constexpr uint32_t kCkenAc97 = 2u, kCkenAc97Config = 31u;
/* Intel PXA27x Developer's Manual Table 13-12 (page 13-28) FIFOE: "Transmit FIFO overrun occurs" if "programmed
   I/O tries to update the transmit FIFO when it is already full"; Table 13-17 (page 13-33): "Receive FIFO
   underrun occurs" if "programmed I/O tries to read the receive FIFO when it is empty". */
constexpr bool kPioFifoErrors = true;

class Pxa27xAc97 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa270;
    }

    /* Intel PXA27x Developer's Manual Table 3-2 (page 3-12): "Any module not listed takes the reset
       value for all of its registers". */
    void OnReady() override {
        clock_  = &emu_.Get<GuestCycleClock>();
        link_  = &emu_.Get<Pxa2xxAc97Link>();
        pcm_    = &emu_.Get<Pxa2xxAc97Pcm>();
        pcm_in_ = &emu_.Get<Pxa2xxAc97PcmIn>();
        mic_in_ = &emu_.Get<Pxa2xxAc97MicIn>();
        modem_  = &emu_.Get<Pxa2xxAc97Modem>();
        modem_out_ = &emu_.Get<Pxa2xxAc97ModemOut>();
        dma_   = &emu_.Get<Pxa2xxDma>();
        codec_ = emu_.TryGet<Ac97Codec>();
        bit_clock_ = &emu_.Get<Pxa2xxAc97BitClock>();
        clocks_    = &emu_.Get<Pxa27xClockManager>();
        irq_       = &emu_.Get<Pxa2xxAc97Interrupt>();
        irq_->Attach([this](uint64_t now) { return IrqLevel(now); }, [this](uint64_t now) { return IrqNext(now); });
        link_->SetReadTimeoutValue(kReadTimeoutValue);
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { ApplyUnitClocks(); });
        clocks_->RegisterClockEnableListener([this] { ApplyUnitClocks(); });
        ApplyUnitClocks();
    }

    uint32_t MmioBase() const override { return 0x40500000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    enum : uint32_t {
        kPOCR = 0x000u, kPCMICR = 0x004u, kMCCR = 0x008u, kGCR = 0x00Cu, kPOSR = 0x010u,
        kPCMISR = 0x014u, kMCSR = 0x018u, kGSR = 0x01Cu, kCAR = 0x020u, kPCDR = 0x040u,
        kMCDR = 0x060u, kMOCR = 0x100u, kMICR = 0x108u, kMOSR = 0x110u, kMISR = 0x118u,
        kMODR = 0x140u,
    };

    bool     InColdReset() const { return (gcr_ & kGcrNcrst) == 0u; }
    uint32_t ReadRegister(uint32_t addr);
    void     WriteRegister(uint32_t addr, uint32_t value);
    uint32_t ReadGsr(uint64_t now);
    void     WriteGcr(uint64_t now, uint32_t value);
    void     ClearRegisters();
    void     ResetLine();
    void     ApplyUnitClocks();
    bool     IrqLevel(uint64_t now);
    uint64_t IrqNext(uint64_t now);

    GuestCycleClock*    clock_     = nullptr;
    Pxa2xxAc97Link*     link_      = nullptr;
    Pxa2xxAc97BitClock* bit_clock_ = nullptr;
    Pxa27xClockManager* clocks_    = nullptr;
    Pxa2xxAc97Pcm*   pcm_    = nullptr;
    Pxa2xxAc97PcmIn* pcm_in_ = nullptr;
    Pxa2xxAc97MicIn* mic_in_ = nullptr;
    Pxa2xxAc97Modem* modem_  = nullptr;
    Pxa2xxAc97ModemOut* modem_out_ = nullptr;
    Pxa2xxDma*       dma_   = nullptr;
    Ac97Codec*       codec_ = nullptr;
    Pxa2xxAc97Interrupt* irq_ = nullptr;

    uint32_t gcr_ = 0, pocr_ = 0, pcmicr_ = 0, mccr_ = 0, mocr_ = 0, micr_ = 0;
    bool     shut_down_ = false;
};

/* Intel PXA27x Developer's Manual Table 13-7 (page 13-13): CKEN[31] / CKEN[2] 0/0 BITCLK disabled, 0/1
   externally provided, 1/0 "AC97_RESET_n signal is asserted, and AC97_BITCLK is the 13-MHz clock", 1/1
   a 13-MHz BITCLK. */
void Pxa27xAc97::ApplyUnitClocks() {
    const uint64_t now    = clock_->Cycles();
    const bool     config = clocks_->ClockEnabled(kCkenAc97Config);
    const bool     unit   = clocks_->ClockEnabled(kCkenAc97);
    bit_clock_->SetInternal(now, config);
    bit_clock_->SetUnitClock(now, unit);
    link_->SetCodecResetPin(now, config && !unit);
}

void Pxa27xAc97::ClearRegisters() {
    pocr_ = pcmicr_ = mccr_ = mocr_ = micr_ = 0u;
    shut_down_ = false;
}

void Pxa27xAc97::ResetLine() {
    if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume() && (gcr_ & kGcrNcrst) != 0u) {
        emu_.Get<Fatal>().Die("Pxa27xAc97: sleep exit with GCR 0x%08X (nCRST set); not modelled", gcr_);
    }
    gcr_ = 0u;
    ClearRegisters();
    link_->SetDmaRequestsMasked(false);
    link_->ResetLine();
}

/* Intel PXA27x Developer's Manual section 13.6.1 (page 13-16): "When GCR[nCRST] is 0b0, all other
   registers are in their reset state." */
uint32_t Pxa27xAc97::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const uint32_t v   = ReadRegister(addr);
    if (Pxa2xxAc97Link::InCodecWindow(off) || off == kPCDR || off == kMCDR || off == kMODR) irq_->Refresh();
    return v;
}

void Pxa27xAc97::WriteWord(uint32_t addr, uint32_t value) {
    WriteRegister(addr, value);
    irq_->Refresh();
}

uint32_t Pxa27xAc97::ReadRegister(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();
    if (Pxa2xxAc97Link::InCodecWindow(off)) return InColdReset() ? 0u : link_->CodecWindowRead(now, off);
    /* Intel PXA27x Developer's Manual Table 13-8 (page 13-22) WRST: "It remains set until the reset completes
       and AC97_BITCLK is seen on the AC-link, after which it clears itself." */
    if (off == kGCR) return gcr_;
    switch (off) {
    case kPOCR: case kPCMICR: case kMCCR: case kPOSR: case kPCMISR: case kMCSR: case kGSR:
    case kCAR: case kPCDR: case kMCDR: case kMOCR: case kMICR: case kMOSR: case kMISR: case kMODR:
        if (InColdReset()) return 0u;
        break;
    default:
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    switch (off) {
    case kPOCR:   return pocr_;
    case kPCMICR: return pcmicr_;
    case kMCCR:   return mccr_;
    case kMOCR:   return mocr_;
    case kMICR:   return micr_;
    case kPOSR:   return pcm_->Status(now) & (kFifoe | kFsr);
    case kPCMISR: return pcm_in_->Status(now) & (kFifoe | kEoc | kFsr);
    case kMCSR:   return mic_in_->Status(now) & (kFifoe | kEoc | kFsr);
    case kMOSR:   return modem_out_->Status(now) & (kFifoe | kFsr);
    case kMISR:   return modem_->Status(now);
    case kGSR:    return ReadGsr(now);
    case kCAR:    return link_->ReadCar(now) ? kCaip : 0u;
    case kPCDR:   return pcm_in_->ReadData(now, kPioFifoErrors);
    case kMCDR:   return mic_in_->ReadData(now, kPioFifoErrors);
    default:      return modem_->ReadData(now, kPioFifoErrors);
    }
}

void Pxa27xAc97::WriteRegister(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();
    if (Pxa2xxAc97Link::InCodecWindow(off)) {
        if (!InColdReset()) link_->CodecWindowWrite(now, off, static_cast<uint16_t>(value));
        return;
    }
    if (off == kGCR) {
        WriteGcr(now, value);
        return;
    }
    switch (off) {
    case kPOCR: case kPCMICR: case kMCCR: case kPOSR: case kPCMISR: case kMCSR: case kGSR:
    case kCAR: case kPCDR: case kMCDR: case kMOCR: case kMICR: case kMOSR: case kMISR: case kMODR:
        if (InColdReset()) return;
        break;
    default:
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    switch (off) {
    case kPOCR:   pocr_   = value & kCtrlIrqEnables; return;
    case kPCMICR: pcmicr_ = value & kCtrlIrqEnables; return;
    case kMCCR:   mccr_   = value & kCtrlIrqEnables; return;
    case kMOCR:   mocr_   = value & kCtrlIrqEnables; return;
    case kMICR:   micr_   = value & kCtrlIrqEnables; return;
    case kPOSR:   pcm_->ClearStatus(now, value & kFifoe); return;
    case kPCMISR: pcm_in_->ClearStatus(now, value & (kFifoe | kEoc)); return;
    case kMCSR:   mic_in_->ClearStatus(now, value & (kFifoe | kEoc)); return;
    case kMOSR:   modem_out_->ClearStatus(now, value & kFifoe); return;
    case kMISR:   modem_->ClearStatus(now, value & (kFifoe | kEoc)); return;
    case kGSR:
        link_->ClearDone(now, (value & kGsrCdone) != 0u, (value & kGsrSdone) != 0u,
                         (value & kGsrRcs) != 0u, (value & kGsrPresint) != 0u, (value & kGsrGsci) != 0u);
        return;
    /* Intel PXA27x Developer's Manual Table 13-14 (page 13-30) CAIP: "Software can also clear this
       bit by writing 0b0 to this bit location". */
    case kCAR:    link_->WriteCar(now, (value & kCaip) != 0u); return;
    case kPCDR:   pcm_->WriteData(now, value, kPioFifoErrors); return;
    /* Intel PXA27x Developer's Manual Table 13-18 (page 13-34) MCDR: "This is a read-only register.
       A write to this register has no effect." */
    case kMCDR:   return;
    /* Intel PXA27x Developer's Manual Table 13-23 (page 13-39) MODAT: "Writing a 32-bit sample to this
       register updates the data into the modem transmit FIFO." */
    default:      modem_out_->WriteData(now, value, kPioFifoErrors); return;
    }
}

uint16_t Pxa27xAc97::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (!Pxa2xxAc97Link::InCodecWindow(off)) HaltUnsupportedAccess("ReadHalf", addr, 0);
    if (InColdReset()) return 0u;
    const uint16_t v = static_cast<uint16_t>(link_->CodecWindowRead(clock_->Cycles(), off));
    irq_->Refresh();
    return v;
}

void Pxa27xAc97::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (!Pxa2xxAc97Link::InCodecWindow(off)) HaltUnsupportedAccess("WriteHalf", addr, value);
    if (InColdReset()) return;
    link_->CodecWindowWrite(clock_->Cycles(), off, value);
    irq_->Refresh();
}

/* Intel PXA27x Developer's Manual Table 13-9 (page 13-24): POINT "Is set to 0b1 if either
   POSR[FIFOE] or POSR[FSR] is 0b1", PIINT and MCINT also on EOC; ACOFFD "Is 0b1 if the AC-link has
   been cleanly shutdown ... It is cleared when GCR[ACOFF] is cleared". */
uint32_t Pxa27xAc97::ReadGsr(uint64_t now) {
    uint32_t v = 0u;
    if (link_->CommandDone(now)) v |= kGsrCdone;
    if (link_->StatusDone(now)) v |= kGsrSdone;
    if (link_->ReadTimedOut(now)) v |= kGsrRcs;
    if (link_->ResumeEvent(now)) v |= kGsrPresint;
    if (link_->CodecReady(now)) v |= kGsrPcrdy;
    if (mic_in_->Status(now) != 0u) v |= kGsrMcint;
    if (pcm_->Status(now) != 0u) v |= kGsrPoint;
    if (pcm_in_->Status(now) != 0u) v |= kGsrPiint;
    if (modem_out_->Status(now) != 0u) v |= kGsrMoint;
    if (modem_->Status(now) != 0u) v |= kGsrMiint;
    if (shut_down_) v |= kGsrAcoffd;
    if (link_->GpioStatus(now)) v |= kGsrGsci;
    return v;
}

/* Intel PXA27x Developer's Manual Table 13-9 (page 13-24) POINT / PIINT and (page 13-25) MOINT / MIINT: "An
   interrupt is triggered if one or more of the following conditions are true"; Table 13-8 (page 13-21) GCR
   CDONE_IE, SDONE_IE, PRDY_IE, PRES_IE and (page 13-22) GPI_IE. */
bool Pxa27xAc97::IrqLevel(uint64_t now) {
    if (InColdReset()) return false;
    const uint32_t gsr = ReadGsr(now);
    const auto on = [gsr](uint32_t enabled, uint32_t status) { return enabled != 0u && (gsr & status) != 0u; };
    const auto channel = [](uint32_t ctrl, uint32_t status) {
        return ((ctrl & kFeie) != 0u && (status & kFifoe) != 0u) || ((ctrl & kFsrie) != 0u && (status & kFsr) != 0u) ||
               (status & kEoc) != 0u;
    };
    return on(gcr_ & kGcrCdoneIe, kGsrCdone) || on(gcr_ & kGcrSdoneIe, kGsrSdone) ||
           on(gcr_ & kGcrPrdyIe, kGsrPcrdy) || on(gcr_ & kGcrPresIe, kGsrPresint) || on(gcr_ & kGcrGpiIe, kGsrGsci) ||
           channel(pocr_, pcm_->Status(now)) || channel(pcmicr_, pcm_in_->Status(now)) ||
           channel(mccr_, mic_in_->Status(now)) || channel(mocr_, modem_out_->Status(now)) ||
           channel(micr_, modem_->Status(now));
}

uint64_t Pxa27xAc97::IrqNext(uint64_t now) {
    uint64_t at = Pxa2xxAc97Link::kNever;
    if (InColdReset()) return at;
    const auto keep = [&at](uint64_t cycle) {
        if (cycle < at) at = cycle;
    };
    const auto watched = [](uint32_t ctrl) {
        return ((ctrl & kFeie) != 0u ? kFifoe : 0u) | ((ctrl & kFsrie) != 0u ? kFsr : 0u);
    };
    if ((gcr_ & kGcrGpiIe) != 0u) keep(link_->NextGpioStatusCycle(now));
    if (watched(pocr_) != 0u) keep(pcm_->NextStatusCycle(now, watched(pocr_)));
    if (watched(pcmicr_) != 0u) keep(pcm_in_->NextStatusCycle(now, watched(pcmicr_)));
    if (watched(mccr_) != 0u) keep(mic_in_->NextStatusCycle(now, watched(mccr_)));
    if (watched(mocr_) != 0u) keep(modem_out_->NextStatusCycle(now, watched(mocr_)));
    if (watched(micr_) != 0u) keep(modem_->NextStatusCycle(now, watched(micr_)));
    return at;
}

/* Intel PXA27x Developer's Manual section 13.6.2 (page 13-17): "Setting GCR[ACOFF] cleanly shuts down
   the AC '97 controller"; "The GCR[nCRST] bit supersedes the GCR[ACOFF] bit and therefore prevents a
   clean shutdown if set during or before the shutdown sequence." */
void Pxa27xAc97::WriteGcr(uint64_t now, uint32_t value) {
    const uint32_t old       = gcr_;
    const bool     cold      = (value & kGcrNcrst) == 0u;
    const bool     acoff_on  = (old & kGcrAcoff) == 0u && (value & kGcrAcoff) != 0u;
    const bool     acoff_off = (old & kGcrAcoff) != 0u && (value & kGcrAcoff) == 0u;
    gcr_ = value & (kGcrNcrst | kGcrAcoff | kGcrIrqEnables | kGcrNdmaen);
    /* Intel PXA27x Developer's Manual Table 13-8 (page 13-21) nDMAEN: "1 = FIFO Service Requests do not cause DMA
       requests". */
    link_->SetDmaRequestsMasked((value & kGcrNdmaen) != 0u);
    if (cold) {
        ClearRegisters();
        link_->SetColdReset(now, true);
        dma_->OnPortChange();
        return;
    }
    link_->SetColdReset(now, false);
    if ((value & kGcrWrst) != 0u) link_->WarmReset(now);
    if (acoff_on) {
        link_->SetLinkOff(now, true, true);
        shut_down_ = true;
    }
    if (acoff_off && shut_down_) {
        link_->SetLinkOff(now, false, false);
        shut_down_ = false;
    }
    dma_->OnPortChange();
}

void Pxa27xAc97::SaveState(StateWriter& w) {
    w.Write("gcr", gcr_);
    w.Write("pocr", pocr_);
    w.Write("pcmicr", pcmicr_);
    w.Write("mccr", mccr_);
    w.Write("mocr", mocr_);
    w.Write("micr", micr_);
    w.Write<uint8_t>("shut_down", shut_down_ ? 1u : 0u);
    if (codec_ != nullptr) codec_->SaveState(w);
    link_->Save(w);
    pcm_->Save(w);
    pcm_in_->Save(w);
    mic_in_->Save(w);
    modem_->Save(w);
    modem_out_->Save(w);
}

void Pxa27xAc97::RestoreState(StateReader& r) {
    uint8_t shut_down = 0;
    r.Read("gcr", gcr_);
    r.Read("pocr", pocr_);
    r.Read("pcmicr", pcmicr_);
    r.Read("mccr", mccr_);
    r.Read("mocr", mocr_);
    r.Read("micr", micr_);
    r.Read("shut_down", shut_down);
    shut_down_ = shut_down != 0u;
    if (codec_ != nullptr) codec_->RestoreState(r);
    link_->Restore(r);
    pcm_->Restore(r);
    pcm_in_->Restore(r);
    mic_in_->Restore(r);
    modem_->Restore(r);
    modem_out_->Restore(r);
}

void Pxa27xAc97::PostRestore() {
    if (codec_ != nullptr) codec_->PostRestore();
    irq_->PostRestore();
}

}  // namespace

REGISTER_SERVICE(Pxa27xAc97);

}
