#include "sa1111_sac.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "sa1111_intc.h"
#include "sa1111_sac_dma.h"
#include "sa1111_sac_frame_clock.h"
#include "sa1111_sac_host_input.h"
#include "sa1111_sac_host_output.h"
#include "sa1111_sac_l3.h"
#include "sa1111_sac_request_lines.h"
#include "sa1111_sac_rx_dma.h"
#include "sa1111_sac_rx_fifo.h"
#include "sa1111_sac_tx_stream.h"
#include "sa1111_sbi.h"
#include "sa1111_system_controller.h"
#include "../../host/audio_activity_widget.h"
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kSacr0Enb = 1u << 0;
constexpr uint32_t kSacr0Rst = 1u << 3;
constexpr uint32_t kSacr1Drec  = 1u << 3;
constexpr uint32_t kSacr1Drpl  = 1u << 4;
/* SA-1111 Developer's Manual Table 7-8 (printed 7-11): bit 5 ENLBF. */
constexpr uint32_t kSacr1Enlbf = 1u << 5;
/* SA-1111 Developer's Manual Table 7-29 (page 7-21) SAITR bits TFS RFS TUR ROR CADT SADR RSTO -
   TDBDA TDBDB RDBDA RDBDB; Table 11-1 (page 11-3): AudTFSR 36, AudRFSR 37, AudTUR 38, AudROR 39,
   AudDTS 40, AudRDD 41, AudSTO 42, AudXmtDmaDoneA 32, AudXmtDmaDoneB 34, AudRcvDmaDoneA 33, B 35. */
constexpr uint8_t kSaitrNone = 0xFFu;
constexpr uint8_t kSaitrSources[12] = {36u, 37u, 38u, 39u, 40u, 41u, 42u, kSaitrNone, 32u, 34u, 33u, 35u};
constexpr uint32_t kSacr0TfthShift = 8u;
constexpr uint32_t kSacr0TfthMask  = 0xFu << kSacr0TfthShift;
constexpr uint32_t kSacr0RfthShift = 12u;
constexpr uint32_t kSacr0RfthMask  = 0xFu << kSacr0RfthShift;

/* SA-1111 Developer's Manual Table 3-3 note 1: "All reserved bits are read back as zero."
   Table 7-7 SACR0 bits 0, 2, 3, 11:8, 15:12; Table 7-8 SACR1 bits 5:0. */
constexpr uint32_t kSacr0Defined = 0xFF0Du;
constexpr uint32_t kSacr1Defined = 0x3Fu;

/* Table 7-31 (printed 7-22): SADR "0x0000680 to0x00006BC"; the 0x660-0x67C gap has no row. */
constexpr uint32_t kUnlistedFirst = 0x60u;
constexpr uint32_t kSadrFirst     = 0x80u;
constexpr uint32_t kSadrLast      = 0xBCu;

constexpr uint32_t kTxDmaFirst = 0x34u;
constexpr uint32_t kRxDmaFirst = 0x48u;

}

bool Sa1111Sac::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111Sac::OnUnitReady() {
    emu_.Get<AudioActivityWidget>().NotePresent();
    stream_ = &emu_.Get<Sa1111SacTxStream>();
    frames_ = &emu_.Get<Sa1111SacFrameClock>();
    rx_     = &emu_.Get<Sa1111SacRxFifo>();
    lines_  = &emu_.Get<Sa1111SacRequestLines>();
    host_    = &emu_.Get<Sa1111SacHostOutput>();
    host_in_ = &emu_.Get<Sa1111SacHostInput>();
    l3_      = &emu_.Get<Sa1111SacL3>();
    dma_    = &emu_.Get<Sa1111SacDma>();
    rx_dma_ = &emu_.Get<Sa1111SacRxDma>();
    dma_->SetFifoControlReader([this] {
        return Sa1111SacDmaControl::Fifo{Enabled(), ThresholdLevel(), (sacr1_ & kSacr1Enlbf) != 0u};
    });
    rx_dma_->SetFifoControlReader([this] {
        return Sa1111SacDmaControl::Fifo{Enabled(), RxThresholdLevel(), false};
    });
    lines_->SetEdgeHandler([this](uint64_t now) { PublishRequestLines(now); });
    dma_->SetChangeListener([this](uint64_t now) { RefreshSourceEdges(now); });
    rx_dma_->SetChangeListener([this](uint64_t now) { RefreshSourceEdges(now); });
    emu_.Get<Sa1111Intc>().RegisterSampler([this] { SyncRequestLines(); });
    emu_.Get<Sa1111SystemController>().RegisterClockListener([this] { OnSystemClockWrite(); });
    clock_ = &emu_.Get<GuestCycleClock>();
    clock_->RegisterRateListener([this] { ApplyFrameRate(clock_->Cycles()); });
    emu_.Get<Hibernation>().RegisterPreSaveListener([this] { CatchUpDma(clock_->Cycles()); });
    ApplyFrameRate(clock_->Cycles());
}

uint32_t Sa1111Sac::UnitReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    switch (off) {
        case 0x00: return sacr0_;
        case 0x04: return sacr1_;
        case 0x0C: return FifoStatus(clock_->Cycles()) | l3_->StatusBits();
        case 0x10: return FifoStatus(clock_->Cycles());
        case 0x1C: return l3_->Address();
        case 0x20: return l3_->ReadData();
        case 0x08: return sacr2_;
        case 0x24: return accar_;
        case 0x28: return accdr_;
        case 0x2C: case 0x30: return 0u;
        case 0x34: case 0x38: case 0x3C: case 0x40: case 0x44:
            return dma_->ReadRegister((off - kTxDmaFirst) >> 2);
        case 0x48: case 0x4C: case 0x50: case 0x54: case 0x58:
            return rx_dma_->ReadRegister((off - kRxDmaFirst) >> 2);
        case 0x5C: return saitr_;
    }
    if (off >= kSadrFirst && off <= kSadrLast) return ReadSadr(clock_->Cycles());
    if (off >= kUnlistedFirst && off < kSadrFirst) return 0u;
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Sa1111Sac::UnitWriteWord(uint32_t addr, uint32_t value) {
    WriteRegister(addr, value);
    PublishRequestLines(clock_->Cycles());
}

void Sa1111Sac::WriteRegister(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kSadrFirst && off <= kSadrLast) {
        WriteSadr(clock_->Cycles(), value);
        return;
    }
    if (off >= kUnlistedFirst && off < kSadrFirst) return;
    switch (off) {
        case 0x00: WriteSacr0(value); return;
        case 0x04: WriteSacr1(value); return;
        case 0x08: sacr2_ = value; return;
        case 0x24: accar_ = value; return;
        case 0x28: accdr_ = value; return;
        case 0x2C: return;
        case 0x18: {                              /* SASCR, Table 7-12. */
            const uint64_t now = clock_->Cycles();
            CatchUpDma(now);
            if (value & (1u << 5)) stream_->ClearUnderrun(now);
            if (value & (1u << 6)) rx_->ClearOverrun(frames_->Position(now));
            l3_->ClearStatus(value);
            return;
        }
        case 0x1C: l3_->WriteAddress(value); return;
        case 0x20: l3_->WriteData(value, sacr1_); return;
        case 0x34: case 0x38: case 0x3C: case 0x40: case 0x44:
            dma_->WriteRegister((off - kTxDmaFirst) >> 2, value);
            return;
        case 0x48: case 0x4C: case 0x50: case 0x54: case 0x58:
            rx_dma_->WriteRegister((off - kRxDmaFirst) >> 2, value);
            return;
        case 0x5C:
            saitr_ = value;
            for (uint32_t bit = 0; bit < 12u; ++bit) {
                if ((value & (1u << bit)) != 0u && kSaitrSources[bit] != kSaitrNone)
                    emu_.Get<Sa1111Intc>().StrobeSource(kSaitrSources[bit]);
            }
            return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

/* SA-1111 Developer's Manual Table 7-7 RST: "Reset the SAC Control and FIFOs except this
   register"; §7.4.1.1 Note: "The following bits are effective when the ENB bit is "1"". */
bool Sa1111Sac::RstActive() const {
    return (sacr0_ & (kSacr0Enb | kSacr0Rst)) == (kSacr0Enb | kSacr0Rst);
}

/* Table 7-8 note: SACR1 "The power-up/reset default value of this register is 0000h";
   §2.3: "Unless indicated otherwise, all register bits are set to zero during reset." */
void Sa1111Sac::ResetRegisters(uint64_t now) {
    dma_->Reset(now);
    rx_dma_->Reset(now);
    stream_->Clear(now);
    rx_->Clear(frames_->Position(now));
    l3_->Reset();
    sacr1_ = sacr2_ = accar_ = accdr_ = saitr_ = 0u;
}

void Sa1111Sac::OnChipReset(bool held) {
    const uint64_t now = clock_->Cycles();
    if (held) {
        ResetRegisters(now);
        sacr0_ = kSacr0Reset;
        UpdateSerializer(now);
    } else {
        ApplyFrameRate(now);
    }
    PublishRequestLines(now);
}

void Sa1111Sac::WriteSacr0(uint32_t value) {
    const uint64_t now = clock_->Cycles();
    value &= kSacr0Defined;
    EvaluateDma(now);
    if (!RstActive() && (value & (kSacr0Enb | kSacr0Rst)) == (kSacr0Enb | kSacr0Rst)) {
        ResetRegisters(now);
    }
    sacr0_ = value;
    UpdateSerializer(now);
    EvaluateDma(now);
}

void Sa1111Sac::WriteSacr1(uint32_t value) {
    sacr1_ = value & kSacr1Defined;
    UpdateSerializer(clock_->Cycles());
}

/* §7.4.22 (printed 7-21): SADR "One register is for WRITES, and transfers data to the Transmit
   FIFO"; Table 7-30: DTL 15:0 left, DTH 31:16 right. */
void Sa1111Sac::WriteSadr(uint64_t now, uint32_t value) {
    dma_->CatchUp(now);
    if (stream_->PushWord(now)) {
        uint8_t frame[4];
        cerf::le::Put32(frame, value);
        host_->Begin();
        host_->Queue(frame, sizeof(frame));
    }
    dma_->Evaluate(now);
}

/* §7.4.22: "the other register is for READS, and takes data from the Receive FIFO". */
uint32_t Sa1111Sac::ReadSadr(uint64_t now) {
    CatchUpDma(now);
    uint8_t word[4] = {};
    if (rx_->Pop(frames_->Position(now))) host_in_->Take(word, 1u);
    PublishRequestLines(now);
    return cerf::le::U32(word);
}

/* Table 7-10 TFS: "0 - Transmit FIFO level exceeds TFL threshold, or SAC disabled". */
bool Sa1111Sac::Enabled() const {
    return (sacr0_ & kSacr0Enb) != 0u && (sacr0_ & kSacr0Rst) == 0u;
}

/* Table 7-8 DRPL: "1 = Replaying Function is Disabled"; Table 7-7 ENB: "1 = Pins
   function as Serial Audio Controller". */
bool Sa1111Sac::SerializerWanted() const {
    return clock_on_ && Enabled() && (sacr1_ & kSacr1Drpl) == 0u;
}

/* Table 7-8 DREC: "0 = Recording Function is Enabled". */
bool Sa1111Sac::RecordingWanted() const {
    return clock_on_ && Enabled() && (sacr1_ & kSacr1Drec) == 0u;
}

void Sa1111Sac::UpdateSerializer(uint64_t now) {
    const bool play   = SerializerWanted();
    const bool record = RecordingWanted();
    if (play == stream_->Running() && record == rx_->Running()) return;
    EvaluateDma(now);
    const uint64_t pos = frames_->Position(now);
    if (record != rx_->Running()) {
        if (record) {
            host_in_->Clear();
            rx_->Start(pos);
        } else {
            rx_->Stop(pos);
        }
    }
    if (play != stream_->Running()) {
        if (play) stream_->Run(now);
        else      stream_->Hold(now);
    }
    EvaluateDma(now);
}

/* Table 7-7 TFTH: "This value should be set to the desired threshold value minus one." */
uint32_t Sa1111Sac::ThresholdLevel() const {
    return ((sacr0_ & kSacr0TfthMask) >> kSacr0TfthShift) + 1u;
}

/* Table 7-7 RFTH: "This value should be set to the desired threshold value minus one." */
uint32_t Sa1111Sac::RxThresholdLevel() const {
    return ((sacr0_ & kSacr0RfthMask) >> kSacr0RfthShift) + 1u;
}

/* Table 7-10 BSY: "1 - SAC currently transmitting or receiving a frame". */
uint32_t Sa1111Sac::FifoStatus(uint64_t now) {
    EvaluateDma(now);
    const uint64_t pos = frames_->Position(now);
    uint32_t status = stream_->StatusBits(now, Enabled(), ThresholdLevel()) |
                      rx_->StatusBits(pos, Enabled(), RxThresholdLevel());
    if (stream_->Running() || rx_->Running()) status |= 1u << 2;
    return status;
}

void Sa1111Sac::EvaluateDma(uint64_t now) {
    dma_->Evaluate(now);
    rx_dma_->Evaluate(now);
}

void Sa1111Sac::CatchUpDma(uint64_t now) {
    dma_->CatchUp(now);
    rx_dma_->CatchUp(now);
}

Sa1111SacRequestLines::Inputs Sa1111Sac::LineInputs() const {
    Sa1111SacRequestLines::Inputs in;
    in.enabled       = Enabled();
    in.tx_threshold  = ThresholdLevel();
    in.rx_threshold  = RxThresholdLevel();
    in.tx_words_left = dma_->WordsRequested();
    in.tx_draining   = dma_->NextRequestCycle(in.tx_next_request);
    in.rx_requests   = rx_dma_->Requests();
    in.rx_draining   = rx_dma_->NextRequestCycle(in.rx_next_request);
    return in;
}

void Sa1111Sac::PublishRequestLines(uint64_t now) {
    CatchUpDma(now);
    lines_->Publish(now, LineInputs());
    RefreshSourceEdges(now);
}

void Sa1111Sac::RefreshSourceEdges(uint64_t now) {
    lines_->RefreshEdges(now, LineInputs());
}

void Sa1111Sac::SyncRequestLines() { PublishRequestLines(clock_->Cycles()); }

void Sa1111Sac::RescaleFrames(uint64_t now, GuestCycleClock::Rate fs) {
    if (!stream_->Rescale(now, clock_->CpuHz(), fs)) {
        emu_.Get<Fatal>().Die("Sa1111Sac: the transmit frame phase does not fit the %llu/%llu "
                              "Hz frame clock", static_cast<unsigned long long>(fs.num),
                              static_cast<unsigned long long>(fs.den));
    }
    host_->SetRate(fs.num, fs.den);
    host_in_->SetRate(fs.num, fs.den);
}

/* SA-1111 Developer's Manual §5.2.1: SKPCR "controls the clock enables for the functional
   blocks"; §2.4.2: "Enabled (active) functional blocks continue to operate, while clocks to
   other blocks are switched off". */
void Sa1111Sac::ApplyFrameRate(uint64_t now) {
    auto& sysc = emu_.Get<Sa1111SystemController>();
    EvaluateDma(now);
    GuestCycleClock::Rate fs;
    clock_on_ = sysc.AudioFrameRate(fs);
    if (clock_on_) {
        RescaleFrames(now, fs);
        UpdateSerializer(now);
    } else {
        UpdateSerializer(now);
        stream_->StopClock(now);
    }
    l3_->OnClockChange(sysc.L3ClockRunning());
    EvaluateDma(now);
}

void Sa1111Sac::OnSystemClockWrite() {
    const uint64_t now = clock_->Cycles();
    ApplyFrameRate(now);
    PublishRequestLines(now);
}

void Sa1111Sac::SaveState(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    w.Write("sacr0", sacr0_); w.Write("sacr1", sacr1_); w.Write("sacr2", sacr2_);
    w.Write("accar", accar_); w.Write("accdr", accdr_); w.Write("saitr", saitr_);
    l3_->Save(w);
    dma_->Save(w);
    frames_->Save(w, now);
    stream_->Save(w, now);
    rx_->Save(w, frames_->Position(now));
    rx_dma_->Save(w);
}

void Sa1111Sac::RestoreState(StateReader& r) {
    const uint64_t now = clock_->Cycles();
    r.Read("sacr0", sacr0_); r.Read("sacr1", sacr1_); r.Read("sacr2", sacr2_);
    r.Read("accar", accar_); r.Read("accdr", accdr_); r.Read("saitr", saitr_);
    l3_->Restore(r);
    dma_->Restore(r, now);
    frames_->Restore(r, now);
    stream_->Restore(r, now);
    rx_->Restore(r, frames_->Position(now));
    rx_dma_->Restore(r, now);
}

void Sa1111Sac::PostRestore() {
    const uint64_t now = clock_->Cycles();
    auto& sysc = emu_.Get<Sa1111SystemController>();
    GuestCycleClock::Rate fs;
    clock_on_ = sysc.AudioFrameRate(fs);
    if (clock_on_) RescaleFrames(now, fs);
    else           stream_->StopClock(now);
    l3_->OnClockChange(sysc.L3ClockRunning());
    lines_->Baseline(now, LineInputs());
    dma_->PostRestore();
    rx_dma_->PostRestore();
    RefreshSourceEdges(now);
}

REGISTER_SERVICE(Sa1111Sac);

}
