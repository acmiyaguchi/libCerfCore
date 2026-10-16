#define NOMINMAX

#include "sa1111_sac_rx_dma.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "sa1111_sac_frame_clock.h"
#include "sa1111_sac_host_input.h"
#include "sa1111_sac_request_lines.h"
#include "sa1111_sac_rx_fifo.h"
#include "sa1111_sac_tx_stream.h"

#include <algorithm>

namespace {

const Sa1111SacDmaChannel::Names kRxNames{
    {"sadrcs", "sadrsa", "sadrca", "sadrsb", "sadrcb"},
    "rx_dma_running", "rx_dma_buffer_b", "rx_dma_words_left", "RX", "SADRCS",
    Sa1111SacRequestLines::kSrcRxDoneA, Sa1111SacRequestLines::kSrcRxDoneB};

}

Sa1111SacRxDma::Sa1111SacRxDma(CerfEmulator& emu) : Sa1111SacDmaChannel(emu, kRxNames) {}

bool Sa1111SacRxDma::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacRxDma::OnChannelReady() {
    frames_  = &emu_.Get<Sa1111SacFrameClock>();
    rx_      = &emu_.Get<Sa1111SacRxFifo>();
    host_in_ = &emu_.Get<Sa1111SacHostInput>();
}

/* SA-1111 Developer's Manual §7.2.2 (printed 7-5): "When data is to be transferred (generally in
   response to a Transmit or Receive FIFO condition), the SAC requests the bus using DMA_Req<1>". */
bool Sa1111SacRxDma::Draining() const {
    return running_ && words_left_ != 0u && granted_ && Fifo().enabled;
}

/* SA-1111 Developer's Manual Table 7-10 (printed 7-14) RFS: "1 - Receive FIFO level is at or
   above RFL threshold"; Table 7-7 RFTH: "Receive FIFO interrupt or DMA threshold". */
bool Sa1111SacRxDma::NextRequest(uint64_t pos, uint64_t cycle, uint32_t level,
                                 uint32_t threshold, uint64_t& at, uint64_t& at_cycle) const {
    if (level >= threshold) {
        at       = pos;
        at_cycle = cycle;
        return true;
    }
    if (!rx_->Running()) return false;
    at       = pos + (threshold - level);
    at_cycle = frames_->CycleOfPosition(at);
    return true;
}

bool Sa1111SacRxDma::NextRequestCycle(uint64_t& cycle) const {
    if (!Draining()) return false;
    uint64_t at = 0;
    return NextRequest(floor_pos_, floor_cycle_, rx_->Level(floor_pos_),
                       Fifo().threshold_level, at, cycle);
}

bool Sa1111SacRxDma::PlannedEnd(uint64_t& cycle) const {
    if (!Draining()) return false;
    const uint32_t threshold = Fifo().threshold_level;
    uint64_t pos      = floor_pos_;
    uint64_t at_cycle = floor_cycle_;
    uint32_t level    = rx_->Level(floor_pos_);
    uint32_t left     = words_left_;
    while (left != 0u) {
        uint64_t at = 0;
        if (!NextRequest(pos, at_cycle, level, threshold, at, at_cycle)) return false;
        if (at != pos) level = threshold;
        const uint32_t count = std::min(threshold, left);
        level -= count;
        left  -= count;
        pos    = at;
    }
    cycle = at_cycle;
    return true;
}

/* SA-1111 Developer's Manual §7.2.2 (printed 7-5): "The SAC drives address onto the bus, asserts
   DWrite if it's a write transfer to DRAM, and then drives data onto the bus". */
void Sa1111SacRxDma::FillTo(uint64_t now) {
    if (!granted_) return;
    const uint32_t threshold = Fifo().threshold_level;
    auto& mem = emu_.Get<EmulatedMemory>();
    uint8_t words[Sa1111SacTxStream::kFifoDepth * 4u];
    while (words_left_ != 0u) {
        uint64_t at = 0, at_cycle = 0;
        if (!NextRequest(floor_pos_, floor_cycle_, rx_->Level(floor_pos_), threshold, at,
                         at_cycle) ||
            at_cycle > now) {
            return;
        }
        const uint32_t count = std::min(threshold, words_left_);
        const uint32_t bytes = 4u * count;
        host_in_->Take(words, count);
        for (uint32_t i = mem.TryCopyIn(address_, words, bytes); i < bytes; ++i) {
            mem.WriteByte(address_ + i, words[i]);
        }
        rx_->Take(at, count);
        address_    += 4u * count;
        words_left_ -= count;
        ++requests_;
        floor_pos_   = at;
        floor_cycle_ = at_cycle;
        end_cycle_   = at_cycle;
    }
}

void Sa1111SacRxDma::MarkFloor(uint64_t now) {
    floor_pos_   = frames_->Position(now);
    floor_cycle_ = now;
}

void Sa1111SacRxDma::SettleTo(uint64_t now) {
    MarkFloor(now);
}

void Sa1111SacRxDma::OnBlockStart(uint64_t, bool buffer_b) {
    address_ = ctl_.Address(buffer_b);
}

void Sa1111SacRxDma::OnBlockDone() {}

bool Sa1111SacRxDma::KeepStarted(bool) const {
    return false;
}

void Sa1111SacRxDma::OnStopped() {}

void Sa1111SacRxDma::OnReset(uint64_t now) {
    MarkFloor(now);
}

Sa1111Sbi::BusGrant Sa1111SacRxDma::CurrentGrant() const {
    return granted_ ? Sa1111Sbi::BusGrant::Granted : Sa1111Sbi::BusGrant::Stalled;
}

void Sa1111SacRxDma::ApplyGrant(uint64_t, Sa1111Sbi::BusGrant grant) {
    granted_ = grant == Sa1111Sbi::BusGrant::Granted;
}

void Sa1111SacRxDma::SaveExtra(StateWriter& w) const {
    w.Write<uint32_t>("rx_dma_address", address_);
}

void Sa1111SacRxDma::RestoreExtra(StateReader& r, uint64_t now) {
    r.Read("rx_dma_address", address_);
    MarkFloor(now);
    host_in_->Clear();
}

void Sa1111SacRxDma::OnRestored() {}

REGISTER_SERVICE(Sa1111SacRxDma);
