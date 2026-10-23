#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apbx.h"
#include "imx28_digctl.h"
#include "imx28_id.h"
#include "imx28_saif.h"
#include "imx28_saif_regs.h"
#include "imx28_saif_host_input.h"
#include "imx28_saif_host_output.h"

#include <cstdint>

namespace cerfcore {

using namespace imx28_saif;

bool Imx28Saif::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Saif::OnReady() {
    apbx_   = &emu_.Get<Imx28Apbx>();
    host_   = &emu_.Get<Imx28SaifHostOutput>();
    mic_    = &emu_.Get<Imx28SaifHostInput>();
    digctl_ = &emu_.Get<Imx28Digctl>();
    if (unit_ == 1u) {
        peer_        = &emu_.Get<Imx28Saif0>();
        peer_->peer_ = this;
    }
    clock_    = &emu_.Get<GuestCycleClock>();
    event_    = clock_->Add([this] { OnBlockEnd(); });
    rx_event_ = clock_->Add([this] { OnRxEvent(); });
    frames_.Attach(1u, 1u);
    clock_->RegisterRateListener([this] { Rescale(); });
    apbx_->AttachDevice(kDmaChannelBase + unit_, this);
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Saif::ResetBlock() {
    if (playing_ != 0u) host_->Finish();
    ctrl_      = kCtrlReset;
    stat_      = 0u;
    playing_   = 0u;
    remaining_ = 0u;
    block_.clear();
    clock_->Disarm(event_);
    ResetRx();
}

void Imx28Saif::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 SAIF%u: %s at %08X (value %08X, CTRL %08X)", unit_, what, addr,
                          value, ctrl_);
}

void Imx28Saif::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    RxSync();
    peer_->RxSync();
    ApplyCtrl(op, value, addr);
    RxUpdate();
    peer_->RxUpdate();
}

void Imx28Saif::ApplyCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        if (!block_.empty() || rx_want_ != 0u) Die("SFTRST with DMA data held", addr, value);
        ResetBlock();
        return;
    }
    if ((next & kCtrlReserved) != 0u) Die("CTRL RSRVD2 bits", addr, value);
    if ((next & kCtrlIrqEnables) != 0u) Die("CTRL FIFO IRQ enable", addr, value);
    if ((next & kCtrlRun) != 0u) {
        if ((old & kCtrlRun) != 0u) {
            if (((next ^ old) & ~kCtrlClkgate) != 0u) Die("CTRL change with RUN set", addr, value);
            if ((next & kCtrlSftrst) != 0u) Die("RUN with SFTRST", addr, value);
        } else if (Gated(next)) {
            Die("RUN with SFTRST or CLKGATE", addr, value);
        }
        if ((next & kCtrlReadMode) != 0u) CheckReceive(next, addr, value);
        if ((next & kCtrlChannelMask) == kCtrlChannelRsvd) {
            Die("RUN with CHANNEL_NUM_SELECT 3", addr, value);
        }
    } else if ((old & kCtrlRun) != 0u && playing_ != 0u) {
        Die("RUN cleared with BUSY set", addr, value);
    }
    ctrl_ = next;
    if (playing_ != 0u) GateClock((old & kCtrlClkgate) != 0u, (next & kCtrlClkgate) != 0u);
    if ((next & kCtrlRun) != 0u && (old & kCtrlRun) == 0u && !block_.empty()) StartBlock();
}

/* Section 35.3.1 CLKGATE gates the SAIF clocks and keeps the configuration; BUSY holds;
   sharp_brain_pw_sh4 nk.exe 80211ad4 sets and restores it. */
void Imx28Saif::GateClock(bool was, bool now) {
    if (!was && now) {
        const uint64_t at = frames_.Now();
        remaining_ = end_ > at ? end_ - at : 0u;
        clock_->Disarm(event_);
    } else if (was && !now) {
        end_ = frames_.Now() + remaining_;
        frames_.ArmAt(event_, end_);
    }
}

/* Section 35.3.2 FIFO_UNDERFLOW_IRQ [6], FIFO_OVERFLOW_IRQ [5]: reset through the SCT clear
   address. */
void Imx28Saif::WriteStat(Imx28SctOp op, uint32_t value, uint32_t addr) {
    if (op != Imx28SctOp::Clr || (value & ~kStatClearable) != 0u) Die("STAT write", addr, value);
    stat_ &= ~value;
}

/* Section 35.3.2 BUSY, FIFO_SERVICE_IRQ, DMA_PREQ; section 35.3.3 null data on an empty FIFO;
   sharp_brain_pw_sh4 wavedev2_BU26154.dll c07a9dc4. */
uint32_t Imx28Saif::ComposeStat() const {
    uint32_t v = kStatPresent | stat_;
    if (playing_ != 0u) v |= kStatBusy;
    if ((ctrl_ & kCtrlReadMode) == 0u && block_.empty()) v |= kStatService | kStatDmaPreq;
    if ((ctrl_ & (kCtrlReadMode | kCtrlRun)) == (kCtrlReadMode | kCtrlRun)) v |= kStatBusy;
    if ((ctrl_ & kCtrlReadMode) != 0u && rx_count_ != 0u) v |= kStatService | kStatDmaPreq;
    return v;
}

uint32_t Imx28Saif::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl && a.op == Imx28SctOp::Write) return ctrl_;
    if (a.reg_offset == kStat && a.op == Imx28SctOp::Write) {
        RxSync();
        return ComposeStat();
    }
    switch (a.reg_offset) {
        case kData:    return ReadData(addr);
        case kVersion: Die("VERSION read", addr, 0u);
        default:       break;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Saif::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    switch (a.reg_offset) {
        case kCtrl: WriteCtrl(a.op, value, addr); return;
        case kStat: WriteStat(a.op, value, addr); return;
        case kData: Die("DATA write", addr, value);
        default:    break;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

/* Section 35.2.2 serialization of each FIFO sample pair at the frame rate. */
void Imx28Saif::StartBlock() {
    const GuestCycleClock::Rate fs = FrameRate();
    frames_.SetOscRate(fs.num, fs.den);
    end_     = frames_.Now() + block_.size() / kFrameBytes;
    playing_ = 1u;
    host_->Queue(block_.data(), static_cast<uint32_t>(block_.size()),
                 static_cast<uint32_t>(fs.RoundedHz()));
    frames_.ArmAt(event_, end_);
}

/* Section 35.3.2 FIFO_UNDERFLOW_IRQ in transmit mode. */
void Imx28Saif::OnBlockEnd() {
    playing_ = 0u;
    block_.clear();
    apbx_->NotifyTransferDone(kDmaChannelBase + unit_);
    if (!block_.empty()) return;
    stat_ |= kStatUnderflow;
    host_->Finish();
}

void Imx28Saif::Rescale() {
    RxSync();
    frames_.Rescale();
    if (playing_ != 0u && (ctrl_ & kCtrlClkgate) == 0u) frames_.ArmAt(event_, end_);
    rx_mark_ = rx_live_ != 0u ? frames_.Now() : rx_mark_;
    RxArm();
}

/* Section 6.2: PIO words go to the device registers from the first offset, as plain writes;
   sharp_brain_pw_sh4 wavedev2_BU26154.dll c07a9a5c. */
void Imx28Saif::DmaPioWrite(uint32_t, uint32_t index, uint32_t value) {
    if (index != 0u) {
        emu_.Get<Fatal>().Die("Imx28 SAIF%u: DMA PIO write, index %u (value %08X, CTRL %08X)",
                              unit_, index, value, ctrl_);
    }
    WriteCtrl(Imx28SctOp::Write, value, base_ + kCtrl);
}

Imx28ApbDmaDevice::Transfer Imx28Saif::DmaToMemory(uint32_t, uint8_t* dst, uint32_t count) {
    if ((ctrl_ & kCtrlReadMode) == 0u) Die("DMA receive in transmit mode, bytes", base_ + kData, count);
    return RxDma(dst, count);
}

/* Section 35.2.2 DMA requests for PCM samples; section 35.3.1 WORD_LENGTH, CHANNEL_NUM_SELECT. */
Imx28ApbDmaDevice::Transfer Imx28Saif::DmaFromMemory(uint32_t, const uint8_t* src, uint32_t count) {
    if ((ctrl_ & kCtrlReadMode) != 0u || Gated(ctrl_)) Die("DMA transmit, bytes", base_ + kData, count);
    if ((ctrl_ & (kCtrlWordLength | kCtrlChannelMask)) != 0u || (count % kFrameBytes) != 0u) {
        Die("DMA transmit format, bytes", base_ + kData, count);
    }
    if (!block_.empty()) Die("DMA transmit with data held, bytes", base_ + kData, count);
    block_.assign(src, src + count);
    if ((ctrl_ & kCtrlRun) != 0u) StartBlock();
    return Transfer{count, false, true};
}

bool Imx28Saif::DmaCommandEnded(uint32_t) { Die("DMA WAIT4ENDCMD", base_, 0u); }

void Imx28Saif::DmaChannelReset(uint32_t) {
    if (playing_ != 0u) host_->Finish();
    playing_ = 0u;
    block_.clear();
    clock_->Disarm(event_);
    rx_want_ = 0u;
    rx_dma_.clear();
    clock_->Disarm(rx_event_);
}

void Imx28Saif::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
    w.Write("stat", stat_);
    w.Write("playing", playing_);
    w.Write("end", end_);
    w.Write("remaining", remaining_);
    w.Write("block_size", static_cast<uint32_t>(block_.size()));
    w.WriteBytes("block", block_.data(), block_.size());
    frames_.Save(w, "frames_");
    SaveRx(w);
}

void Imx28Saif::RestoreState(StateReader& r) {
    uint32_t size = 0u;
    r.Read("ctrl", ctrl_);
    r.Read("stat", stat_);
    r.Read("playing", playing_);
    r.Read("end", end_);
    r.Read("remaining", remaining_);
    r.Read("block_size", size);
    block_.resize(size);
    r.ReadBytes("block", block_.data(), block_.size());
    frames_.Restore(r, "frames_");
    RestoreRx(r);
}

void Imx28Saif::PostRestore() {
    host_->Stop();
    if (playing_ != 0u && (ctrl_ & kCtrlClkgate) == 0u) frames_.ArmAt(event_, end_);
    else                                                 clock_->Disarm(event_);
    mic_->Clear();
    clock_->Disarm(rx_event_);
    RxArm();
}

}
