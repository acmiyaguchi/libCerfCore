#include "pxa2xx_ac97_link.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/ac97_codec.h"
#include <cerfcore/state/state_stream.h>
#include "../pxa255/pxa255_id.h"
#include "../pxa27x/pxa270_id.h"
#include "pxa2xx_ac97_bit_clock.h"

namespace cerfcore {

namespace {

constexpr uint32_t kRegPowerdown  = 0x26u;
constexpr uint32_t kRegGpioStatus = 0x54u;

}  // namespace

bool Pxa2xxAc97Link::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Pxa255 || bd->GetSocId() == SocId::Pxa270);
}

void Pxa2xxAc97Link::OnReady() {
    clock_     = &emu_.Get<GuestCycleClock>();
    codec_     = emu_.TryGet<Ac97Codec>();
    bit_clock_ = &emu_.Get<Pxa2xxAc97BitClock>();
    notify_ev_ = clock_->Add([this] { NotifyEvent(); });
    bit_clock_->SetChangeListener([this](uint64_t at) { OnBitClock(at); });
    clock_->RegisterRateListener([this] { OnCpuRate(); });
    if (codec_ != nullptr) codec_->AttachFrameSource(this);
}

bool Pxa2xxAc97Link::FrameAt(uint64_t cycle, uint64_t& frame) const {
    if (!running_) return false;
    frame = FrameIndexAt(cycle);
    return true;
}

void Pxa2xxAc97Link::OnCodecStreamChange() { NotifyEvent(); }

/* Intel PXA255 Developer's Manual section 13.5.2.1 (page 13-13): "To wake up the AC-link, a CODEC drives
   SDATA_IN to a logic high level. The rising edge triggers the Resume Interrupt". */
void Pxa2xxAc97Link::CodecWake(uint64_t cycle, bool codec_restarts) {
    Settle(cycle);
    resume_ = true;
    if (codec_restarts) bit_clock_->CodecPoweredUp(cycle);
    clock_->Arm(notify_ev_, cycle);
}

void Pxa2xxAc97Link::CodecGpioInterrupt(uint64_t frame, bool level) {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    if (level && !gpi_) gpi_frame_ = frame;
    gpi_ = level;
    clock_->Arm(notify_ev_, now);
}

void Pxa2xxAc97Link::NotifyEvent() {
    for (Pxa2xxAc97LinkListener* l : listeners_) l->OnLinkEvent();
}

/* Intel PXA27x Developer's Manual Table 13-9 (page 13-25) GSCI: "Is 0b1 whenever bit 0 of slot 12 is 0b1";
   Intel PXA255 Developer's Manual Table 13-8 (page 13-23) GSCI: "1 = Bit 0 of slot 12 is set". */
void Pxa2xxAc97Link::Settle(uint64_t now) {
    if (running_ && gpi_ && !gsci_ && FrameIndexAt(now) > gpi_frame_) gsci_ = true;
}

/* Intel PXA255 Developer's Manual Table 13-8 (page 13-22): CDONE "ACUNIT has sent command
   address and data to the CODEC", SDONE "ACUNIT has received status address and data from the
   CODEC"; Table 13-13 (page 13-26) CAIP: "Once the cycle is complete, this bit is automatically cleared". */
void Pxa2xxAc97Link::CompleteCommand(uint64_t at) {
    const Cmd cmd = cmd_;
    cmd_  = Cmd::None;
    caip_ = false;
    const uint64_t frame = FrameIndexAt(at);
    if (cmd == Cmd::Read) {
        /* Intel PXA255 Developer's Manual section 13.4.1.2 (page 13-7): "if no response is
           received, the ACUNIT returns a dummy read completion (0xFFFF_FFFF) to the CPU and sets
           the Read Completion Status (RDCS) bit". */
        if (cmd_absent_) {
            latch_ = timeout_value_;
            rdcs_  = true;
        } else {
            latch_ = codec_->ReadReg(cmd_reg_, frame);
        }
        sdone_ = true;
        return;
    }
    if (cmd_absent_) {
        cdone_ = true;
        return;
    }
    codec_->WriteReg(cmd_reg_, cmd_value_, frame);
    LOG(SocIis, "AC-link codec write 0x%02X <= 0x%04X at cycle %llu\n", cmd_reg_, cmd_value_,
        static_cast<unsigned long long>(at));
    cdone_ = true;
    for (Pxa2xxAc97LinkListener* l : listeners_) l->OnCodecWrite(at);
    clock_->Arm(notify_ev_, at);
    /* Cirrus Logic WM9713L Rev 4.0 Figure 4 (page 13): with PR4 set, BITCLK goes low at most
       1.0 us after the end of slot 2. */
    if (cmd_reg_ == kRegPowerdown && codec_->LinkPoweredDown()) {
        bit_clock_->CodecPoweredDown(at);
        if (!stopping_) UpdateRun(at);
    }
    const GuestCycleClock::Rate r = bit_clock_->Rate();
    const bool moved = !bit_clock_->Running() || r != bits_rate_;
    if (running_ && !stopping_ && moved) OnBitClock(at);
}

void Pxa2xxAc97Link::UpdateRun(uint64_t now) {
    const bool bitclk = bit_clock_->Running();
    const bool run    = codec_ != nullptr && cold_released_ && bitclk && !off_;
    if (run == running_) return;
    LOG(SocIis, "AC-link %s at cycle %llu (cold released %d, BITCLK %d, link off %d)\n",
        run ? "runs" : "stops", static_cast<unsigned long long>(now), cold_released_ ? 1 : 0,
        bitclk ? 1 : 0, off_ ? 1 : 0);
    if (!run) {
        const uint64_t frame = FrameIndexAt(now);
        if (frame != 0u) gpio_shadow_ = codec_->GpioSlotStatus(frame - 1u);
        stopping_ = true;
        for (Pxa2xxAc97LinkListener* l : listeners_) l->OnLinkStop(now);
        running_  = false;
        stopping_ = false;
        if (cold_released_) codec_->LinkStopped(FrameIndexAt(now));
        return;
    }
    running_ = true;
    SetBitRate(now, false);
    bits_.Start(now);
    ready_     = true;
    gpi_frame_ = FrameIndexAt(now) + 1u;
    codec_->LinkStarted(FrameIndexAt(now));
    if (cmd_ != Cmd::None) CompleteCommand(now);
    for (Pxa2xxAc97LinkListener* l : listeners_) l->OnLinkRun(now);
}

void Pxa2xxAc97Link::SetBitRate(uint64_t now, bool rescale) {
    bits_rate_ = bit_clock_->Rate();
    if (rescale) {
        bits_.Rescale(now, clock_->ClockRate(), bits_rate_);
    } else {
        bits_.SetRate(clock_->ClockRate(), bits_rate_);
    }
}

void Pxa2xxAc97Link::OnBitClock(uint64_t at) {
    Settle(at);
    const GuestCycleClock::Rate r = bit_clock_->Rate();
    if (running_ && bit_clock_->Running() && r != bits_rate_) {
        SetBitRate(at, true);
    }
    UpdateRun(at);
    NotifyEvent();
}

void Pxa2xxAc97Link::OnCpuRate() {
    if (!running_) return;
    const uint64_t now = clock_->Cycles();
    Settle(now);
    SetBitRate(now, true);
    NotifyEvent();
}

void Pxa2xxAc97Link::SetColdReset(uint64_t now, bool asserted) {
    Settle(now);
    if (asserted != cold_released_) return;
    if (asserted) {
        AssertColdReset(now);
        return;
    }
    cold_released_ = true;
    if (!reset_pin_) bit_clock_->ColdStart(now);
}

/* Intel PXA27x Developer's Manual Table 13-7 (page 13-13): with CKEN[31] 1 and CKEN[2] 0 "AC97_RESET_n
   signal is asserted". */
void Pxa2xxAc97Link::SetCodecResetPin(uint64_t now, bool asserted) {
    Settle(now);
    if (asserted == reset_pin_) return;
    reset_pin_ = asserted;
    if (!asserted) {
        if (cold_released_) bit_clock_->ColdStart(now);
        return;
    }
    bit_clock_->Stop(now);
    UpdateRun(now);
    gpi_ = false;
    if (codec_ != nullptr) codec_->ColdReset();
}

/* Intel PXA255 Developer's Manual Table 13-7 (page 13-21) COLD_RST: "Causes a cold reset to
   occur throughout the AC'97 circuitry. All data in the ACUNIT and the CODEC will be lost";
   section 13.5.2.2.1 (page 13-14): "All AC'97 control registers are initialized to their default". */
void Pxa2xxAc97Link::AssertColdReset(uint64_t now) {
    bit_clock_->Stop(now);
    cold_released_ = false;
    off_           = false;
    requests_off_  = false;
    UpdateRun(now);
    cmd_        = Cmd::None;
    cmd_absent_ = false;
    latch_ = 0u;
    cdone_ = sdone_ = rdcs_ = caip_ = ready_ = resume_ = gpi_ = gsci_ = false;
    gpio_shadow_ = 0u;
    for (Pxa2xxAc97LinkListener* l : listeners_) l->OnFifoReset(now, true);
    if (codec_) codec_->ColdReset();
}

void Pxa2xxAc97Link::SetLinkOff(uint64_t now, bool off, bool discard_fifos) {
    Settle(now);
    if (off == off_) return;
    off_          = off;
    requests_off_ = off && discard_fifos;
    UpdateRun(now);
    if (!requests_off_) return;
    for (Pxa2xxAc97LinkListener* l : listeners_) l->OnFifoReset(now, false);
}

/* AC '97 Component Specification Revision 2.1 section 7 (page 47): a warm reset will "restart
   AC '97's digital interface (resetting PR4 to zero)". */
bool Pxa2xxAc97Link::WarmReset(uint64_t now) {
    Settle(now);
    if (!cold_released_ || reset_pin_) return false;
    return bit_clock_->WarmReset(now);
}

/* Intel PXA27x Developer's Manual section 13.6.3 (page 13-17): "Software issues a dummy read
   to the Codec register. The AC '97 controller responds to this read operation with invalid
   data. The AC '97 controller then initiates the read access across the AC-link." */
uint32_t Pxa2xxAc97Link::CodecRead(uint64_t now, uint32_t reg, bool absent) {
    Settle(now);
    const uint32_t previous = latch_;
    cmd_        = Cmd::Read;
    cmd_reg_    = reg;
    cmd_absent_ = absent;
    if (running_) CompleteCommand(now);
    return previous;
}

void Pxa2xxAc97Link::CodecWrite(uint64_t now, uint32_t reg, uint16_t value, bool absent) {
    Settle(now);
    cmd_        = Cmd::Write;
    cmd_reg_    = reg;
    cmd_value_  = value;
    cmd_absent_ = absent;
    if (running_) CompleteCommand(now);
}

/* Intel PXA255 Developer's Manual section 13.8.3.17 (page 13-32): Primary Audio CODEC at 0x4050_0200,
   Secondary Audio at 0x4050_0300, Primary Modem at 0x4050_0400, Secondary Modem at 0x4050_0500, each
   "+ Shift_Left_Once(Internal 7-bit CODEC Register Address)". */
bool Pxa2xxAc97Link::SecondaryWindow(uint32_t off) { return (((off - kCodecBase) >> 8) & 1u) != 0u; }

/* Intel PXA255 Developer's Manual section 13.6 (page 13-14): the modem CODEC GPIO register at 0x0054
   "write operation goes across the AC-link, but a read does not"; its contents "are continuously updated
   into a shadow register in the ACUNIT when a frame is received from the CODEC". */
uint32_t Pxa2xxAc97Link::CodecWindowRead(uint64_t now, uint32_t off) {
    const uint32_t reg = (off & 0xFFu) >> 1;
    if (reg != kRegGpioStatus || off - kCodecBase < kModemWindow) {
        return CodecRead(now, reg, SecondaryWindow(off));
    }
    Settle(now);
    if (SecondaryWindow(off)) return 0u;
    if (!running_) return gpio_shadow_;
    const uint64_t frame = FrameIndexAt(now);
    return frame == 0u ? gpio_shadow_ : codec_->GpioSlotStatus(frame - 1u);
}

void Pxa2xxAc97Link::CodecWindowWrite(uint64_t now, uint32_t off, uint16_t value) {
    CodecWrite(now, (off & 0xFFu) >> 1, value, SecondaryWindow(off));
}

void Pxa2xxAc97Link::RequireCodec(const char* fifo, const char* access) {
    if (codec_ != nullptr) return;
    emu_.Get<Fatal>().Die("Pxa2xxAc97 %s FIFO: programmed I/O %s with no AC'97 codec on this board; not modelled",
                          fifo, access);
}

/* Intel PXA255 Developer's Manual Table 13-13 (page 13-26) CAIP: "No cycle is in progress and
   the act of reading the register sets this bit to '1'". */
bool Pxa2xxAc97Link::ReadCar(uint64_t now) {
    Settle(now);
    const bool was = caip_;
    caip_ = true;
    return was;
}

void Pxa2xxAc97Link::WriteCar(uint64_t now, bool caip) {
    Settle(now);
    caip_ = caip;
}

bool Pxa2xxAc97Link::CommandDone(uint64_t now) {
    Settle(now);
    return cdone_;
}

bool Pxa2xxAc97Link::StatusDone(uint64_t now) {
    Settle(now);
    return sdone_;
}

/* Intel PXA255 Developer's Manual Table 13-8 (page 13-22) RDCS: "The bit remains set until cleared
   by software. This bit is cleared by software writing a '1' to this location." */
bool Pxa2xxAc97Link::ReadTimedOut(uint64_t now) {
    Settle(now);
    return rdcs_;
}

/* Intel PXA255 Developer's Manual Table 13-8 (page 13-22) PCR: "Reflects the state of the
   CODEC ready bit in SDATA_IN_0". */
bool Pxa2xxAc97Link::CodecReady(uint64_t now) {
    Settle(now);
    return ready_;
}

/* Intel PXA255 Developer's Manual Table 13-8 (page 13-22) PRIRES: "A resume event occurred on the
   SDATA_IN_0. This bit is cleared by software writing a '1' to this location." */
bool Pxa2xxAc97Link::ResumeEvent(uint64_t now) {
    Settle(now);
    return resume_;
}

bool Pxa2xxAc97Link::GpioStatus(uint64_t now) {
    Settle(now);
    return gsci_;
}

void Pxa2xxAc97Link::ClearDone(uint64_t now, bool command, bool status, bool timeout, bool resume, bool gpio) {
    Settle(now);
    if (command) cdone_ = false;
    if (status) sdone_ = false;
    if (timeout) rdcs_ = false;
    if (resume) resume_ = false;
    if (!gpio) return;
    gsci_ = false;
    if (running_) gpi_frame_ = FrameIndexAt(now);
}

uint64_t Pxa2xxAc97Link::NextGpioStatusCycle(uint64_t now) {
    Settle(now);
    if (!running_ || !gpi_ || gsci_) return kNever;
    return CycleOfFrame(gpi_frame_ + 1u);
}

void Pxa2xxAc97Link::ResetLine() {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    bit_clock_->ResetLine();
    AssertColdReset(now);
}

void Pxa2xxAc97Link::Save(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    const RatedTickCount::Position pos = running_ ? bits_.PositionAt(now) : RatedTickCount::Position{};
    bit_clock_->Save(w);
    w.Write<uint8_t>("link_cold_released", cold_released_ ? 1u : 0u);
    w.Write<uint8_t>("link_reset_pin", reset_pin_ ? 1u : 0u);
    w.Write<uint8_t>("link_off", off_ ? 1u : 0u);
    w.Write<uint8_t>("link_requests_off", requests_off_ ? 1u : 0u);
    w.Write<uint8_t>("link_dma_masked", dma_masked_ ? 1u : 0u);
    w.Write<uint8_t>("link_running", running_ ? 1u : 0u);
    w.Write<uint8_t>("link_ready", ready_ ? 1u : 0u);
    w.Write<uint8_t>("link_cmd", static_cast<uint8_t>(cmd_));
    w.Write<uint8_t>("link_cmd_absent", cmd_absent_ ? 1u : 0u);
    w.Write<uint32_t>("link_cmd_reg", cmd_reg_);
    w.Write<uint16_t>("link_cmd_value", cmd_value_);
    w.Write<uint32_t>("link_read_data", latch_);
    w.Write<uint8_t>("link_cdone", cdone_ ? 1u : 0u);
    w.Write<uint8_t>("link_sdone", sdone_ ? 1u : 0u);
    w.Write<uint8_t>("link_rdcs", rdcs_ ? 1u : 0u);
    w.Write<uint8_t>("link_caip", caip_ ? 1u : 0u);
    w.Write<uint8_t>("link_resume", resume_ ? 1u : 0u);
    w.Write<uint8_t>("link_gpi", gpi_ ? 1u : 0u);
    w.Write<uint8_t>("link_gsci", gsci_ ? 1u : 0u);
    w.Write<uint64_t>("link_gpi_frame", gpi_frame_);
    w.Write<uint16_t>("link_gpio_shadow", gpio_shadow_);
    w.Write<uint64_t>("link_bits_ticks", pos.ticks);
    w.Write<uint64_t>("link_bits_phase", pos.phase);
    w.Write<uint64_t>("link_bits_phase_den", pos.phase_den);
}

void Pxa2xxAc97Link::Restore(StateReader& r) {
    uint8_t released = 0, pin = 0, off = 0, requests_off = 0, running = 0, ready = 0, cmd = 0;
    uint8_t absent = 0, dma_masked = 0;
    uint8_t cdone = 0, sdone = 0, rdcs = 0, caip = 0, resume = 0, gpi = 0, gsci = 0;
    RatedTickCount::Position pos;
    bit_clock_->Restore(r);
    r.Read("link_cold_released", released);
    r.Read("link_reset_pin", pin);
    r.Read("link_off", off);
    r.Read("link_requests_off", requests_off);
    r.Read("link_dma_masked", dma_masked);
    r.Read("link_running", running);
    r.Read("link_ready", ready);
    r.Read("link_cmd", cmd);
    r.Read("link_cmd_absent", absent);
    r.Read("link_cmd_reg", cmd_reg_);
    r.Read("link_cmd_value", cmd_value_);
    r.Read("link_read_data", latch_);
    r.Read("link_cdone", cdone);
    r.Read("link_sdone", sdone);
    r.Read("link_rdcs", rdcs);
    r.Read("link_caip", caip);
    r.Read("link_resume", resume);
    r.Read("link_gpi", gpi);
    r.Read("link_gsci", gsci);
    r.Read("link_gpi_frame", gpi_frame_);
    r.Read("link_gpio_shadow", gpio_shadow_);
    r.Read("link_bits_ticks", pos.ticks);
    r.Read("link_bits_phase", pos.phase);
    r.Read("link_bits_phase_den", pos.phase_den);
    cold_released_ = released != 0u;
    reset_pin_     = pin != 0u;
    off_           = off != 0u;
    requests_off_  = requests_off != 0u;
    dma_masked_    = dma_masked != 0u;
    running_       = running != 0u;
    ready_         = ready != 0u;
    cmd_           = static_cast<Cmd>(cmd);
    cmd_absent_    = absent != 0u;
    cdone_         = cdone != 0u;
    sdone_         = sdone != 0u;
    rdcs_          = rdcs != 0u;
    caip_          = caip != 0u;
    resume_        = resume != 0u;
    gpi_           = gpi != 0u;
    gsci_          = gsci != 0u;
    if (!running_) return;
    bits_rate_ = bit_clock_->Rate();
    bits_.SetRate(clock_->ClockRate(), bits_rate_);
    bits_.PlaceAt(clock_->Cycles(), pos);
}

REGISTER_SERVICE(Pxa2xxAc97Link);

}
