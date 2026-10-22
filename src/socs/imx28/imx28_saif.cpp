#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apbx.h"
#include "imx28_clkctrl.h"
#include "imx28_id.h"
#include "imx28_saif.h"
#include "imx28_saif_host_output.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Section 35.3 memory map. */
constexpr uint32_t kCtrl    = 0x00u;
constexpr uint32_t kStat    = 0x10u;
constexpr uint32_t kData    = 0x20u;
constexpr uint32_t kVersion = 0x30u;
constexpr uint32_t kMapEnd  = 0x40u;

/* Section 35.3.1 CTRL: reset C000_0000h; RSRVD2 [23:21]. */
constexpr uint32_t kCtrlReset    = 0xC0000000u;
constexpr uint32_t kCtrlReserved = 0x00E00000u;

/* Section 35.3.1 FIFO_ERROR_IRQ_EN [25], FIFO_SERVICE_IRQ_EN [24], CHANNEL_NUM_SELECT [15:14]
   (11 reserved), READ_MODE [1], RUN [0]. */
constexpr uint32_t kCtrlIrqEnables   = 0x03000000u;
constexpr uint32_t kCtrlChannelMask  = 0x0000C000u;
constexpr uint32_t kCtrlChannelRsvd  = 0x0000C000u;
constexpr uint32_t kCtrlReadMode     = 1u << 1;
constexpr uint32_t kCtrlRun          = 1u << 0;

/* Section 35.3.1 BITCLK_BASE_RATE [26], WORD_LENGTH [7:4] 0000 16-bit; section 35.2.1 saif_clk
   is 512 or 384 times the sample rate. */
constexpr uint32_t kCtrlBaseRate   = 1u << 26;
constexpr uint32_t kCtrlWordLength = 0x000000F0u;

/* Section 35.3.2 STAT: reset 8000_0000h; DMA_PREQ [16], FIFO_UNDERFLOW_IRQ [6],
   FIFO_OVERFLOW_IRQ [5], FIFO_SERVICE_IRQ [4], BUSY [0]. */
constexpr uint32_t kStatPresent   = 1u << 31;
constexpr uint32_t kStatDmaPreq   = 1u << 16;
constexpr uint32_t kStatUnderflow = 1u << 6;
constexpr uint32_t kStatOverflow  = 1u << 5;
constexpr uint32_t kStatService   = 1u << 4;
constexpr uint32_t kStatBusy      = 1u << 0;
constexpr uint32_t kStatClearable = kStatUnderflow | kStatOverflow | kStatService;

/* Table 7-1: APBX channel 4 SAIF0, channel 5 SAIF1. */
constexpr uint32_t kDmaChannelBase = 4u;

/* Section 35.2.2: 16-bit sample pairs, one FIFO entry per frame. */
constexpr uint32_t kFrameBytes = 4u;

}  /* namespace */

bool Imx28Saif::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Saif::OnReady() {
    apbx_  = &emu_.Get<Imx28Apbx>();
    host_  = &emu_.Get<Imx28SaifHostOutput>();
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { OnBlockEnd(); });
    frames_.Attach(1u, 1u);
    clock_->RegisterRateListener([this] { Rescale(); });
    apbx_->AttachDevice(kDmaChannelBase + unit_, this);
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Saif::ResetBlock() {
    ctrl_    = kCtrlReset;
    stat_    = 0u;
    playing_ = 0u;
    block_.clear();
    clock_->Disarm(event_);
}

void Imx28Saif::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 SAIF%u: %s at %08X (value %08X, CTRL %08X)", unit_, what, addr,
                          value, ctrl_);
}

void Imx28Saif::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        if (!block_.empty()) Die("SFTRST with DMA data held", addr, value);
        ResetBlock();
        return;
    }
    if ((next & kCtrlReserved) != 0u) Die("CTRL RSRVD2 bits", addr, value);
    if ((next & kCtrlIrqEnables) != 0u) Die("CTRL FIFO IRQ enable", addr, value);
    if ((next & kCtrlRun) != 0u) {
        if ((old & kCtrlRun) != 0u && next != old) Die("CTRL change with RUN set", addr, value);
        if (Gated(next)) Die("RUN with SFTRST or CLKGATE", addr, value);
        if ((next & kCtrlReadMode) != 0u) Die("RUN in READ_MODE", addr, value);
        if ((next & kCtrlChannelMask) == kCtrlChannelRsvd) {
            Die("RUN with CHANNEL_NUM_SELECT 3", addr, value);
        }
    } else if ((old & kCtrlRun) != 0u && playing_ != 0u) {
        Die("RUN cleared with BUSY set", addr, value);
    }
    ctrl_ = next;
    if ((next & kCtrlRun) != 0u && (old & kCtrlRun) == 0u && !block_.empty()) StartBlock();
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
    return v;
}

uint32_t Imx28Saif::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl && a.op == Imx28SctOp::Write) return ctrl_;
    if (a.reg_offset == kStat && a.op == Imx28SctOp::Write) return ComposeStat();
    switch (a.reg_offset) {
        case kData:    Die("DATA read", addr, 0u);
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

/* Section 35.2.1: sample rate is saif_clk over 512 (BITCLK_BASE_RATE 0) or 384 (1); section
   35.2.2 serialization of each FIFO sample pair at that rate. */
void Imx28Saif::StartBlock() {
    const uint32_t over = (ctrl_ & kCtrlBaseRate) != 0u ? 384u : 512u;
    const GuestCycleClock::Rate fs =
        emu_.Get<Imx28Clkctrl>().SaifRate(unit_).Scaled(1u, over).Reduced();
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
    frames_.Rescale();
    if (playing_ != 0u) frames_.ArmAt(event_, end_);
}

void Imx28Saif::DmaPioWrite(uint32_t, uint32_t index, uint32_t value) {
    Die("DMA PIO write, index", index, value);
}

Imx28ApbDmaDevice::Transfer Imx28Saif::DmaToMemory(uint32_t, uint8_t*, uint32_t count) {
    Die("DMA receive, bytes", base_ + kData, count);
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
}

void Imx28Saif::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
    w.Write("stat", stat_);
    w.Write("playing", playing_);
    w.Write("end", end_);
    w.Write("block_size", static_cast<uint32_t>(block_.size()));
    w.WriteBytes("block", block_.data(), block_.size());
    frames_.Save(w, "frames_");
}

void Imx28Saif::RestoreState(StateReader& r) {
    uint32_t size = 0u;
    r.Read("ctrl", ctrl_);
    r.Read("stat", stat_);
    r.Read("playing", playing_);
    r.Read("end", end_);
    r.Read("block_size", size);
    block_.resize(size);
    r.ReadBytes("block", block_.data(), block_.size());
    frames_.Restore(r, "frames_");
}

void Imx28Saif::PostRestore() {
    host_->Stop();
    if (playing_ != 0u) frames_.ArmAt(event_, end_);
    else                clock_->Disarm(event_);
}

}
