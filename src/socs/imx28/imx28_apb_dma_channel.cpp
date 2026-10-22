#include "imx28_apb_dma_channel.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 Table 6-3, Figure 7-3 and sections 6.5.10, 7.5.8: command word 1 fields. */
constexpr uint32_t kCmdKind      = 0x3u;
constexpr uint32_t kCmdChain     = 1u << 2;
constexpr uint32_t kCmdIrq       = 1u << 3;
constexpr uint32_t kCmdNandLock  = 1u << 4;
constexpr uint32_t kCmdNandReady = 1u << 5;
constexpr uint32_t kCmdSemaphore = 1u << 6;
constexpr uint32_t kCmdWait4End  = 1u << 7;
constexpr uint32_t kCmdHot       = 1u << 8;
constexpr uint32_t kCmdWordsShift = 12u;
constexpr uint32_t kCmdWordsMask  = 0xFu;
constexpr uint32_t kCmdCountShift = 16u;

/* Table 6-2, Table 7-2: COMMAND 00 NO_DMA_XFER, 01 DMA_WRITE, 10 DMA_READ. */
constexpr uint32_t kKindNone  = 0u;
constexpr uint32_t kKindWrite = 1u;
constexpr uint32_t kKindRead  = 2u;

/* Sections 6.5.10, 7.5.8: XFER_COUNT 0 indicates a 64 KBytes transfer. */
constexpr uint32_t kZeroCount = 0x10000u;

/* Sections 6.5.12, 7.5.10: 8 bit counting semaphore, PHORE [23:16]. sharp_brain_pw_sh4 cspddk.dll
   c0602b58 writes INCREMENT_SEMA (256 - PHORE) & 0xFF. */
constexpr uint32_t kSemaMask = 0xFFu;

/* Section 6.2, Figure 6-2: command structure of three words, then PIO words. */
constexpr uint32_t kHeaderBytes = 12u;

}  /* namespace */

/* Sections 6.5.13, 7.5.11 CHn_DEBUG1 reset 00A0_0000h (RD_FIFO_EMPTY, WR_FIFO_EMPTY); STATEMACHINE
   0x00 IDLE, 0x0D READ_REQ, 0x15 WAIT_END, 0x1D HALT_AFTER_TERM, 0x1E CHECK_WAIT. */
uint32_t Imx28ApbDmaChannel::Debug1() const {
    constexpr uint32_t kFifosEmpty = 0x00A00000u;
    switch (state_) {
        case State::Idle:               return kFifosEmpty | 0x00u;
        case State::WaitEnd:            return kFifosEmpty | 0x15u;
        case State::HaltAfterTerminate: return kFifosEmpty | 0x1Du;
        case State::ReadRequest:        return kFifosEmpty | 0x0Du;
        case State::CheckWait:          return kFifosEmpty | 0x1Eu;
        default:                        break;
    }
    Die("DEBUG1 read in state", static_cast<uint32_t>(state_));
}

Imx28ApbDmaChannel::Imx28ApbDmaChannel(CerfEmulator& emu, const Imx28ApbDmaSpec& spec,
                                       uint32_t mmio_base, uint32_t index,
                                       Imx28ApbDmaArbiter& arbiter)
    : emu_(emu), spec_(spec), mmio_base_(mmio_base), index_(index), arbiter_(arbiter) {}

void Imx28ApbDmaChannel::Die(const char* what, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: channel %u (%s) %s %08X", mmio_base_, index_,
                          spec_.channel[index_].usage, what, value);
}

void Imx28ApbDmaChannel::Attach(Imx28ApbDmaDevice* device) {
    if (device_ != nullptr) Die("second device attached", sema_);
    device_ = device;
}

void Imx28ApbDmaChannel::Reset() {
    cur_ = nxt_ = cmd_ = bar_ = sema_ = 0u;
    state_ = State::Idle;
    visited_.clear();
    if (device_ != nullptr) device_->DmaChannelReset(index_);
}

void Imx28ApbDmaChannel::IncrementSemaphore(uint32_t count) {
    sema_ = (sema_ + count) & kSemaMask;
    Run();
}

void Imx28ApbDmaChannel::Resume() { Run(); }

void Imx28ApbDmaChannel::EndCommand() {
    if (state_ != State::WaitEnd) Die("end command in state", static_cast<uint32_t>(state_));
    state_ = State::Idle;
    if (Complete(cmd_)) Run();
}

void Imx28ApbDmaChannel::TransferDone() {
    if (state_ != State::ReadRequest) Die("transfer done in state", static_cast<uint32_t>(state_));
    state_ = State::Idle;
    if (Complete(cmd_)) Run();
}

void Imx28ApbDmaChannel::ReadGuest(uint32_t pa, void* dst, uint32_t bytes) const {
    const uint32_t got = emu_.Get<EmulatedMemory>().TryCopyOut(pa, static_cast<uint8_t*>(dst),
                                                                bytes);
    if (got != bytes) Die("AHB read failed at", pa);
}

void Imx28ApbDmaChannel::WriteGuest(uint32_t pa, const void* src, uint32_t bytes) const {
    const uint32_t put = emu_.Get<EmulatedMemory>().TryCopyIn(pa, static_cast<const uint8_t*>(src),
                                                               bytes);
    if (put != bytes) Die("AHB write failed at", pa);
}

void Imx28ApbDmaChannel::Run() {
    visited_.clear();
    for (;;) {
        if (state_ == State::WaitEnd || state_ == State::HaltAfterTerminate ||
            state_ == State::ReadRequest || state_ == State::CheckWait) {
            return;
        }
        if (sema_ == 0u) {
            state_ = State::Idle;
            return;
        }
        if (!arbiter_.ChannelGranted(index_)) {
            state_ = State::Pending;
            return;
        }
        if (device_ == nullptr) Die("started with no device model, semaphore", sema_);
        Descriptor d;
        Fetch(d);
        if (!Execute(d)) return;
        if (!Complete(d.cmd)) return;
    }
}

void Imx28ApbDmaChannel::Fetch(Descriptor& d) {
    if ((nxt_ & 0x3u) != 0u) Die("unaligned command address", nxt_);
    if (std::find(visited_.begin(), visited_.end(), nxt_) != visited_.end()) {
        Die("chain revisits command with no semaphore decrement at", nxt_);
    }
    visited_.push_back(nxt_);
    uint32_t header[3];
    ReadGuest(nxt_, header, kHeaderBytes);
    cur_ = nxt_;
    nxt_ = header[0];
    cmd_ = d.cmd = header[1];
    bar_ = d.bar = header[2];
    const uint32_t words = (d.cmd >> kCmdWordsShift) & kCmdWordsMask;
    if (words != 0u) ReadGuest(cur_ + kHeaderBytes, d.pio, words * 4u);
}

bool Imx28ApbDmaChannel::Execute(const Descriptor& d) {
    const uint32_t cmd = d.cmd;
    if ((cmd & spec_.cmd_reserved_mask) != 0u) Die("reserved command bits", cmd);
    const bool nand = ((spec_.nand_channels >> index_) & 1u) != 0u;
    if (nand && (cmd & (kCmdNandLock | kCmdNandReady)) != 0u) Die("NAND lock or ready wait", cmd);
    const uint32_t words = (cmd >> kCmdWordsShift) & kCmdWordsMask;
    for (uint32_t i = 0; i < words; ++i) device_->DmaPioWrite(index_, i, d.pio[i]);
    switch (cmd & kCmdKind) {
        case kKindNone:  break;
        case kKindWrite:
        case kKindRead:  if (MoveData(d)) return false; break;
        default:         Die("command", cmd);
    }
    if ((cmd & kCmdWait4End) != 0u && !device_->DmaCommandEnded(index_)) {
        state_ = State::WaitEnd;
        return false;
    }
    return true;
}

bool Imx28ApbDmaChannel::MoveData(const Descriptor& d) {
    uint32_t count = d.cmd >> kCmdCountShift;
    if (count == 0u) count = kZeroCount;
    std::vector<uint8_t> buf(count);
    Imx28ApbDmaDevice::Transfer t{};
    if ((d.cmd & kCmdKind) == kKindWrite) {
        t = device_->DmaToMemory(index_, buf.data(), count);
        if (t.bytes > count || (!t.terminated && t.bytes != count)) Die("device wrote bytes", t.bytes);
        WriteGuest(d.bar, buf.data(), t.bytes);
    } else {
        ReadGuest(d.bar, buf.data(), count);
        t = device_->DmaFromMemory(index_, buf.data(), count);
        if (t.bytes > count || (!t.terminated && t.bytes != count)) Die("device read bytes", t.bytes);
        if (t.deferred) {
            if (t.terminated || (d.cmd & kCmdWait4End) != 0u) Die("deferred transfer, command", d.cmd);
            state_ = State::ReadRequest;
            return true;
        }
    }
    return t.terminated && Terminated(d.cmd);
}

/* Section 6.2 HALTONTERMINATE; section 6.5.3 CTRL2 TERMINATION. */
bool Imx28ApbDmaChannel::Terminated(uint32_t cmd) {
    if (!spec_.has_hot) Die("device terminate, command", cmd);
    arbiter_.DeviceTerminated(index_);
    if ((cmd & kCmdHot) == 0u) return false;
    state_ = State::HaltAfterTerminate;
    return true;
}

/* Section 6.2: semaphore decrement after the end command, interrupt status bit as the last act,
   Table 6-3 CHAIN; section 6.5.13 CHECK_WAIT. */
bool Imx28ApbDmaChannel::Complete(uint32_t cmd) {
    if ((cmd & kCmdSemaphore) != 0u) {
        if (sema_ == 0u) Die("semaphore decrement at zero, command", cmd);
        --sema_;
        visited_.clear();
    }
    if ((cmd & kCmdIrq) != 0u) arbiter_.CommandComplete(index_);
    if (sema_ == 0u) {
        state_ = State::Idle;
        return false;
    }
    if ((cmd & kCmdChain) != 0u) return true;
    state_ = State::CheckWait;
    return false;
}

void Imx28ApbDmaChannel::Save(StateWriter& w) const {
    w.Write("cur", cur_);
    w.Write("nxt", nxt_);
    w.Write("cmd", cmd_);
    w.Write("bar", bar_);
    w.Write("sema", sema_);
    w.Write("state", state_);
}

void Imx28ApbDmaChannel::Restore(StateReader& r) {
    r.Read("cur", cur_);
    r.Read("nxt", nxt_);
    r.Read("cmd", cmd_);
    r.Read("bar", bar_);
    r.Read("sema", sema_);
    r.Read("state", state_);
    visited_.clear();
}

}
