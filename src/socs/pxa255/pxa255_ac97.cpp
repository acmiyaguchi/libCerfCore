#include "pxa255_ac97.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/ac97_codec.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../pxa2xx/pxa2xx_ac97_interrupt.h"
#include "../pxa2xx/pxa2xx_ac97_link.h"
#include "../pxa2xx/pxa2xx_ac97_mic_in.h"
#include "../pxa2xx/pxa2xx_ac97_modem.h"
#include "../pxa2xx/pxa2xx_ac97_modem_out.h"
#include "../pxa2xx/pxa2xx_ac97_pcm.h"
#include "../pxa2xx/pxa2xx_ac97_pcm_in.h"
#include "../pxa2xx/pxa2xx_dma.h"
#include "pxa255_id.h"

REGISTER_SERVICE(Pxa255Ac97);

namespace {

/* Intel PXA255 Developer's Manual Table 13-7 (pages 13-20, 13-21) GCR: CDONE_IE 19, SDONE_IE 18,
   SECRDY_IEN 9, PRIRDY_IEN 8, SECRES_IEN 5, PRIRES_IEN 4, ACLINK_OFF 3, WARM_RST 2, COLD_RST 1, GIE 0. */
constexpr uint32_t kGcrColdRst  = 1u << 1;
constexpr uint32_t kGcrWarmRst  = 1u << 2;
constexpr uint32_t kGcrLinkOff  = 1u << 3;
constexpr uint32_t kGcrCdoneIe  = 1u << 19, kGcrSdoneIe = 1u << 18, kGcrPrirdyIen = 1u << 8;
constexpr uint32_t kGcrPriresIen = 1u << 4, kGcrGie = 1u << 0;
constexpr uint32_t kGcrIrqEnables = kGcrCdoneIe | kGcrSdoneIe | (1u << 9) | kGcrPrirdyIen | (1u << 5) |
                                    kGcrPriresIen | kGcrGie;
/* Intel PXA255 Developer's Manual Table 13-8 (pages 13-22, 13-23) GSR: CDONE 19, SDONE 18, RDCS 15,
   PRIRES 10, PCR 8, MINT 7, POINT 6, PIINT 5, MOINT 2, MIINT 1, GSCI 0. */
constexpr uint32_t kGsrCdone  = 1u << 19;
constexpr uint32_t kGsrSdone  = 1u << 18;
constexpr uint32_t kGsrRdcs   = 1u << 15;
constexpr uint32_t kGsrPrires = 1u << 10;
constexpr uint32_t kGsrPcr    = 1u << 8;
constexpr uint32_t kGsrMint  = 1u << 7;
constexpr uint32_t kGsrPoint = 1u << 6;
constexpr uint32_t kGsrPiint = 1u << 5;
constexpr uint32_t kGsrMoint = 1u << 2;
constexpr uint32_t kGsrMiint = 1u << 1;
constexpr uint32_t kGsrGsci  = 1u << 0;
/* Intel PXA255 Developer's Manual Tables 13-9 and 13-10 (pages 13-23, 13-24), 13-15 (page 13-27),
   13-18 (page 13-29) and 13-19 (page 13-30): FEIE bit 3; Table 13-13 (page 13-26): CAIP bit 0. */
constexpr uint32_t kFeie = 1u << 3;
constexpr uint32_t kCaip = 1u << 0;
/* Intel PXA255 Developer's Manual section 13.4.1.2 (page 13-7): "returns a dummy read completion
   (0xFFFF_FFFF) to the CPU". */
constexpr uint32_t kReadTimeoutValue = 0xFFFFFFFFu;
/* Intel PXA255 Developer's Manual Table 13-11 (page 13-25) POSR FIFOE: "set if a transmit FIFO underrun occurs";
   Table 13-12 (page 13-25) PISR and Table 13-16 (page 13-28) MCSR FIFOE: "set if a receive FIFO overrun
   occurs". */
constexpr bool kPioFifoErrors = false;

}  // namespace

bool Pxa255Ac97::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255Ac97::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    link_  = &emu_.Get<Pxa2xxAc97Link>();
    pcm_    = &emu_.Get<Pxa2xxAc97Pcm>();
    pcm_in_ = &emu_.Get<Pxa2xxAc97PcmIn>();
    mic_in_ = &emu_.Get<Pxa2xxAc97MicIn>();
    modem_  = &emu_.Get<Pxa2xxAc97Modem>();
    modem_out_ = &emu_.Get<Pxa2xxAc97ModemOut>();
    dma_   = &emu_.Get<Pxa2xxDma>();
    codec_ = emu_.TryGet<Ac97Codec>();
    irq_   = &emu_.Get<Pxa2xxAc97Interrupt>();
    irq_->Attach([this](uint64_t now) { return IrqLevel(now); }, [this](uint64_t now) { return IrqNext(now); });
    link_->SetReadTimeoutValue(kReadTimeoutValue);
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if ((gcr_ & kGcrColdRst) != 0u) link_->SetColdReset(clock_->Cycles(), false);
    });
}

/* Intel PXA255 Developer's Manual section 3.4.2 (page 3-7) and section 3.4.3 (page 3-8): the
   watchdog and GPIO resets reset every unit outside the RTC, the Clocks and Power Manager and the
   Memory Controller. */
/* Intel PXA255 Developer's Manual Table 13-7 (page 13-21) COLD_RST: "The value of this bit is retained after
   suspends"; Table 2-6 (page 2-15): nACRESET is "Driven Low" in the sleep state. */
void Pxa255Ac97::ResetLine() {
    const bool released = emu_.Get<GuestCpuReset>().DeliveredResetWasResume() && (gcr_ & kGcrColdRst) != 0u;
    pocr_ = picr_ = mccr_ = mocr_ = micr_ = 0u;
    gcr_  = released ? kGcrColdRst : 0u;
    link_->ResetLine();
}

bool Pxa255Ac97::IsRegister(uint32_t off) {
    switch (off) {
    case kPOCR: case kPICR: case kMCCR: case kGCR: case kPOSR: case kPISR: case kMCSR: case kGSR: case kCAR:
    case kPCDR: case kMCDR: case kMOCR: case kMICR: case kMOSR: case kMISR: case kMODR:
        return true;
    default:
        return false;
    }
}

/* Intel PXA255 Developer's Manual section 13.6.1 (page 13-15): until COLD_RST is set "all other
   registers remain in a reset state". */
uint32_t Pxa255Ac97::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const uint32_t v   = ReadRegister(addr);
    if (Pxa2xxAc97Link::InCodecWindow(off) || off == kPCDR || off == kMCDR || off == kMODR) irq_->Refresh();
    return v;
}

void Pxa255Ac97::WriteWord(uint32_t addr, uint32_t value) {
    WriteRegister(addr, value);
    irq_->Refresh();
}

uint32_t Pxa255Ac97::ReadRegister(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();
    if (Pxa2xxAc97Link::InCodecWindow(off)) return (gcr_ & kGcrColdRst) == 0u ? 0u : link_->CodecWindowRead(now, off);
    if (!IsRegister(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
    /* Intel PXA255 Developer's Manual Table 13-7 (page 13-21) WARM_RST: "it remains set until the reset
       completes and BITCLK is seen on the AC-link after which it clears itself". */
    if (off == kGCR) return gcr_;
    if ((gcr_ & kGcrColdRst) == 0u) return 0u;
    switch (off) {
    case kPOCR: return pocr_;
    case kPICR: return picr_;
    case kMCCR: return mccr_;
    case kMOCR: return mocr_;
    case kMICR: return micr_;
    case kPOSR: return pcm_->Status(now) & Pxa2xxAc97OutFifo::kFifoe;
    case kPISR: return pcm_in_->Status(now) & Pxa2xxAc97InFifo::kFifoe;
    case kMCSR: return mic_in_->Status(now) & Pxa2xxAc97InFifo::kFifoe;
    case kGSR:  return ReadGsr(now);
    case kCAR: return link_->ReadCar(now) ? kCaip : 0u;
    /* Intel PXA255 Developer's Manual Table 13-20 (page 13-30) MOSR: FIFOE "is set if a transmit FIFO
       underrun occurs"; bits 3:0 reserved. */
    case kMOSR: return modem_out_->Status(now) & Pxa2xxAc97OutFifo::kFifoe;
    /* Intel PXA255 Developer's Manual Table 13-21 (page 13-31) MISR: FIFOE "is set if a receive
       FIFO overrun occurs"; bits 3:0 reserved. */
    case kMISR: return modem_->Status(now) & Pxa2xxAc97InFifo::kFifoe;
    case kMODR: return modem_->ReadData(now, kPioFifoErrors);
    /* Intel PXA255 Developer's Manual Table 13-14 (page 13-26): "Reading this register gets a
       32-bit sample from the PCM Receive FIFO." */
    case kPCDR: return pcm_in_->ReadData(now, kPioFifoErrors);
    /* Intel PXA255 Developer's Manual section 13.8.3.11 (page 13-28): "A read to this register gets a 32-bit
       sample from the Mic-in Receive FIFO." */
    case kMCDR: return mic_in_->ReadData(now, kPioFifoErrors);
    default:    HaltUnsupportedAccess("ReadWord", addr, 0);
    }
}

uint32_t Pxa255Ac97::ReadGsr(uint64_t now) {
    uint32_t v = 0u;
    if (link_->CommandDone(now)) v |= kGsrCdone;
    if (link_->StatusDone(now)) v |= kGsrSdone;
    if (link_->ReadTimedOut(now)) v |= kGsrRdcs;
    if (link_->ResumeEvent(now)) v |= kGsrPrires;
    if (link_->CodecReady(now)) v |= kGsrPcr;
    if ((mic_in_->Status(now) & Pxa2xxAc97InFifo::kFifoe) != 0u) v |= kGsrMint;
    if ((pcm_->Status(now) & Pxa2xxAc97OutFifo::kFifoe) != 0u) v |= kGsrPoint;
    if ((pcm_in_->Status(now) & Pxa2xxAc97InFifo::kFifoe) != 0u) v |= kGsrPiint;
    if ((modem_out_->Status(now) & Pxa2xxAc97OutFifo::kFifoe) != 0u) v |= kGsrMoint;
    if ((modem_->Status(now) & Pxa2xxAc97InFifo::kFifoe) != 0u) v |= kGsrMiint;
    if (link_->GpioStatus(now)) v |= kGsrGsci;
    return v;
}

/* Intel PXA255 Developer's Manual Table 13-7 (page 13-20) CDONE_IE, SDONE_IE, PRIRDY_IEN, (page 13-21)
   PRIRES_IEN, GIE; Table 13-9 (page 13-24) FEIE: "An interrupt will occur if bit 4 in the POSR is set". */
bool Pxa255Ac97::IrqLevel(uint64_t now) {
    if ((gcr_ & kGcrColdRst) == 0u) return false;
    const uint32_t gsr = ReadGsr(now);
    const auto on = [gsr](uint32_t enabled, uint32_t status) { return enabled != 0u && (gsr & status) != 0u; };
    return on(gcr_ & kGcrCdoneIe, kGsrCdone) || on(gcr_ & kGcrSdoneIe, kGsrSdone) ||
           on(gcr_ & kGcrPrirdyIen, kGsrPcr) || on(gcr_ & kGcrPriresIen, kGsrPrires) ||
           on(gcr_ & kGcrGie, kGsrGsci) || on(pocr_ & kFeie, kGsrPoint) || on(picr_ & kFeie, kGsrPiint) ||
           on(mccr_ & kFeie, kGsrMint) || on(mocr_ & kFeie, kGsrMoint) || on(micr_ & kFeie, kGsrMiint);
}

uint64_t Pxa255Ac97::IrqNext(uint64_t now) {
    uint64_t at = Pxa2xxAc97Link::kNever;
    if ((gcr_ & kGcrColdRst) == 0u) return at;
    const auto keep = [&at](uint64_t cycle) {
        if (cycle < at) at = cycle;
    };
    if ((gcr_ & kGcrGie) != 0u) keep(link_->NextGpioStatusCycle(now));
    if ((pocr_ & kFeie) != 0u) keep(pcm_->NextStatusCycle(now, Pxa2xxAc97OutFifo::kFifoe));
    if ((picr_ & kFeie) != 0u) keep(pcm_in_->NextStatusCycle(now, Pxa2xxAc97InFifo::kFifoe));
    if ((mccr_ & kFeie) != 0u) keep(mic_in_->NextStatusCycle(now, Pxa2xxAc97InFifo::kFifoe));
    if ((mocr_ & kFeie) != 0u) keep(modem_out_->NextStatusCycle(now, Pxa2xxAc97OutFifo::kFifoe));
    if ((micr_ & kFeie) != 0u) keep(modem_->NextStatusCycle(now, Pxa2xxAc97InFifo::kFifoe));
    return at;
}

void Pxa255Ac97::WriteRegister(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    const uint64_t now = clock_->Cycles();
    if (Pxa2xxAc97Link::InCodecWindow(off)) {
        if ((gcr_ & kGcrColdRst) != 0u) link_->CodecWindowWrite(now, off, static_cast<uint16_t>(value));
        return;
    }
    if (!IsRegister(off)) HaltUnsupportedAccess("WriteWord", addr, value);
    if (off == kGCR) {
        WriteGcr(now, value);
        return;
    }
    if ((gcr_ & kGcrColdRst) == 0u) return;
    switch (off) {
    case kPOCR: WriteControl(pocr_, value); return;
    case kPICR: WriteControl(picr_, value); return;
    case kMCCR: WriteControl(mccr_, value); return;
    case kMOCR: WriteControl(mocr_, value); return;
    case kMICR: WriteControl(micr_, value); return;
    case kGSR:
        link_->ClearDone(now, (value & kGsrCdone) != 0u, (value & kGsrSdone) != 0u,
                         (value & kGsrRdcs) != 0u, (value & kGsrPrires) != 0u, (value & kGsrGsci) != 0u);
        return;
    case kPOSR: pcm_->ClearStatus(now, value & Pxa2xxAc97OutFifo::kFifoe); return;
    case kMOSR: modem_out_->ClearStatus(now, value & Pxa2xxAc97OutFifo::kFifoe); return;
    case kPISR: pcm_in_->ClearStatus(now, value & Pxa2xxAc97InFifo::kFifoe); return;
    case kMCSR: mic_in_->ClearStatus(now, value & Pxa2xxAc97InFifo::kFifoe); return;
    /* Table 13-13 (page 13-26): CAR "is a read/write register"; CAIP "Software can clear this bit by
       writing a '0' to this bit location". */
    case kCAR:  link_->WriteCar(now, (value & kCaip) != 0u); return;
    case kMISR: modem_->ClearStatus(now, value & Pxa2xxAc97InFifo::kFifoe); return;
    /* Table 13-14 (page 13-26): "Writing a 32-bit sample to this register updates the data into the
       PCM Transmit FIFO." */
    case kPCDR: pcm_->WriteData(now, value, kPioFifoErrors); return;
    /* Table 13-22 (page 13-31): "A 32-bit sample write to this register updates the data into the
       Modem Transmit FIFO." */
    case kMODR: modem_out_->WriteData(now, value, kPioFifoErrors); return;
    /* Section 13.8.3.11 (page 13-28): "A write to this register has no effect." */
    case kMCDR: return;
    default:    HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

uint16_t Pxa255Ac97::ReadHalf(uint32_t addr) {
    if (!Pxa2xxAc97Link::InCodecWindow(addr - MmioBase())) HaltUnsupportedAccess("ReadHalf", addr, 0);
    return static_cast<uint16_t>(ReadWord(addr));
}

void Pxa255Ac97::WriteHalf(uint32_t addr, uint16_t value) {
    if (!Pxa2xxAc97Link::InCodecWindow(addr - MmioBase())) HaltUnsupportedAccess("WriteHalf", addr, value);
    WriteWord(addr, value);
}

void Pxa255Ac97::WriteControl(uint32_t& reg, uint32_t value) { reg = value & kFeie; }

/* Intel PXA255 Developer's Manual Table 13-7 (page 13-21) COLD_RST: "0 = Causes a cold reset to occur
   throughout the AC'97 circuitry. All data in the ACUNIT and the CODEC will be lost"; WARM_RST
   "is self clearing"; ACLINK_OFF "0 = If the AC-link was off, turns it back on". */
void Pxa255Ac97::WriteGcr(uint64_t now, uint32_t value) {
    const bool cold = (value & kGcrColdRst) == 0u;
    if (cold) pocr_ = picr_ = mccr_ = mocr_ = micr_ = 0u;
    gcr_ = value & (kGcrColdRst | kGcrLinkOff | kGcrIrqEnables);
    link_->SetColdReset(now, cold);
    if ((value & kGcrWarmRst) != 0u) link_->WarmReset(now);
    link_->SetLinkOff(now, (value & kGcrLinkOff) != 0u, false);
    dma_->OnPortChange();
}

void Pxa255Ac97::SaveState(StateWriter& w) {
    w.Write("pocr", pocr_);
    w.Write("picr", picr_);
    w.Write("mccr", mccr_);
    w.Write("gcr", gcr_);
    w.Write("mocr", mocr_);
    w.Write("micr", micr_);
    if (codec_ != nullptr) codec_->SaveState(w);
    link_->Save(w);
    pcm_->Save(w);
    pcm_in_->Save(w);
    mic_in_->Save(w);
    modem_->Save(w);
    modem_out_->Save(w);
}

void Pxa255Ac97::RestoreState(StateReader& r) {
    r.Read("pocr", pocr_);
    r.Read("picr", picr_);
    r.Read("mccr", mccr_);
    r.Read("gcr", gcr_);
    r.Read("mocr", mocr_);
    r.Read("micr", micr_);
    if (codec_ != nullptr) codec_->RestoreState(r);
    link_->Restore(r);
    pcm_->Restore(r);
    pcm_in_->Restore(r);
    mic_in_->Restore(r);
    modem_->Restore(r);
    modem_out_->Restore(r);
}

void Pxa255Ac97::PostRestore() {
    if (codec_ != nullptr) codec_->PostRestore();
    irq_->PostRestore();
}
