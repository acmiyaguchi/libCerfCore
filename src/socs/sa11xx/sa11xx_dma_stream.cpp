#include "sa11xx_dma_stream.h"

#include <cerfcore/state/state_stream.h>
#include "sa11xx_dma_port.h"

#include <algorithm>

namespace cerfcore {

using namespace sa11xx_dma;

namespace {

/* SA-1110 Developer's Manual §11.6.1.4 / §11.6.1.6: DBTx count 12:0 "in bytes". */
constexpr uint32_t kCountMask = 0x1FFFu;

}

/* §11.6.1.2 (printed 11-10): STRTA "causes the buffer A transfer to begin. This bit is functional
   only if the RUN bit is set"; RUN: "Operation can then be resumed by again setting the RUN bit". */
uint32_t Sa11xxDmaStream::Kick(const Sa11xxDmaChannelRegs& r, uint32_t newly_set) {
    if ((r.dcsr & kRun) == 0u) return 0u;
    const uint32_t starting = (newly_set & kRun) != 0u ? (r.dcsr & (kStrtA | kStrtB))
                                                       : (newly_set & (kStrtA | kStrtB));
    uint32_t started = 0u;
    for (const bool b : {false, true}) {
        const uint32_t i = b ? 1u : 0u;
        if ((starting & Strt(b)) == 0u || in_flight_[i]) continue;
        start_[i]     = b ? r.dbsb : r.dbsa;
        count_[i]     = (b ? r.dbtb : r.dbta) & kCountMask;
        in_flight_[i] = true;
        started |= Strt(b);
    }
    return started;
}

void Sa11xxDmaStream::Release(uint32_t cleared) {
    if ((cleared & kStrtA) != 0u) in_flight_[0] = false;
    if ((cleared & kStrtB) != 0u) in_flight_[1] = false;
}

uint32_t Sa11xxDmaStream::Datum() const {
    return port_->DatumBytes();
}

uint32_t Sa11xxDmaStream::Words(bool b) const {
    return Count(b) / Datum();
}

/* §11.6.2 (printed 11-13): "A context switch may occur when a channel completes a command or
   when a particular burst (portion of a transfer) has been completed". */
uint64_t Sa11xxDmaStream::Supply(const Sa11xxDmaChannelRegs& r) const {
    if ((r.dcsr & kRun) == 0u || !active_) return 0u;
    uint64_t words = end_ - std::min(end_, port_->Moved());
    const bool other = !cur_b_;
    if (in_flight_[other ? 1 : 0] && words % kBurstDatums == 0u) {
        words += Words(other) - moved_[other ? 1 : 0];
    }
    return words;
}

/* §11.6 (printed 11-6): "buffer A and buffer B, can be chained together so that when a transfer
   to (or from) one buffer completes, the transfer to (or from) the other begins immediately." */
void Sa11xxDmaStream::Begin(Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    cur_b_ = (r.dcsr & kBiu) != 0u;
    const uint32_t words = Words(cur_b_);
    origin_ = chained_ ? end_ : port_->Moved();
    end_    = origin_ + words;
    active_ = true;
    announce_ = !port_->Receive() && !port_->LoopedBack();
    Announce(r, hooks);
}

void Sa11xxDmaStream::Announce(const Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    if (!announce_) return;
    const GuestCycleClock::Rate rate = port_->WordRate();
    if (rate.num == 0u) return;
    announce_ = false;
    hooks.transmit(r.ddar, Start(cur_b_), Words(cur_b_) * Datum(), rate);
}

void Sa11xxDmaStream::WriteReceived(const Sa11xxDmaChannelRegs& r, const Hooks& hooks,
                                    uint32_t upto) {
    uint32_t& written = written_[cur_b_ ? 1 : 0];
    if (upto <= written) return;
    const uint32_t pa    = Start(cur_b_) + written * Datum();
    const uint32_t bytes = (upto - written) * Datum();
    if (!port_->FillReceived(pa, bytes)) hooks.receive(r.ddar, pa, bytes, port_->WordRate());
    written = upto;
}

/* §11.6.1.4: DBTAn "contains the current transfer count in bytes for buffer A"; Linux
   sa11x0-dma (sa11x0_dma_pos, sa11x0_dma_suspend / sa11x0_dma_resume) reads DBSx as the live
   position and writes the saved DBSx / DBTx back to continue a paused transfer. */
void Sa11xxDmaStream::CommitPointer(Sa11xxDmaChannelRegs& r, uint32_t words) {
    const uint32_t i     = cur_b_ ? 1u : 0u;
    const uint32_t bytes = std::min(words * Datum(), count_[i]);
    start_[i] += bytes;
    count_[i] -= bytes;
    if (cur_b_) {
        r.dbsb = start_[i];
        r.dbtb = count_[i];
    } else {
        r.dbsa = start_[i];
        r.dbta = count_[i];
    }
    moved_[i]   = 0u;
    written_[i] = 0u;
}

/* §11.6.1.2: DONEA "indicates that the transfer into or out of buffer A has completed";
   "When DONEA is set, STRTA is cleared"; BIU: "This bit is toggled by the DMA controller when
   DONEA or DONEB are set." */
void Sa11xxDmaStream::Complete(Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    const uint32_t words = static_cast<uint32_t>(end_ - origin_);
    if (port_->Receive()) WriteReceived(r, hooks, words);
    CommitPointer(r, words);
    r.dcsr |= Done(cur_b_);
    r.dcsr &= ~Strt(cur_b_);
    r.dcsr ^= kBiu;
    in_flight_[cur_b_ ? 1 : 0] = false;
    active_   = false;
    chained_  = true;
    announce_ = false;
}

void Sa11xxDmaStream::Evaluate(uint64_t now, Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    port_->Settle(now);
    if (active_) Announce(r, hooks);
    for (;;) {
        for (;;) {
            if (active_ && port_->Moved() >= end_) {
                Complete(r, hooks);
                continue;
            }
            if (!active_ && (r.dcsr & kRun) != 0u && in_flight_[(r.dcsr & kBiu) != 0u ? 1 : 0]) {
                Begin(r, hooks);
                continue;
            }
            break;
        }
        port_->SetSupply(now, Supply(r));
        if (!active_ || port_->Moved() < end_) break;
    }
    if (active_) moved_[cur_b_ ? 1 : 0] = static_cast<uint32_t>(port_->Moved() - origin_);
}

void Sa11xxDmaStream::FlushReceived(const Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    if (active_ && port_->Receive()) WriteReceived(r, hooks, moved_[cur_b_ ? 1 : 0]);
}

void Sa11xxDmaStream::StopActive(uint64_t now, Sa11xxDmaChannelRegs& r, const Hooks& hooks) {
    Evaluate(now, r, hooks);
    if (!active_) return;
    const uint32_t moved = moved_[cur_b_ ? 1 : 0];
    if (port_->Receive()) WriteReceived(r, hooks, moved);
    CommitPointer(r, moved);
    active_   = false;
    chained_  = false;
    announce_ = false;
    port_->SetSupply(now, 0u);
}

void Sa11xxDmaStream::Reset() {
    port_     = nullptr;
    active_   = false;
    chained_  = false;
    announce_ = false;
    cur_b_   = false;
    origin_ = 0u;
    end_    = 0u;
    moved_[0] = moved_[1] = 0u;
    written_[0] = written_[1] = 0u;
    start_[0] = start_[1] = 0u;
    count_[0] = count_[1] = 0u;
    in_flight_[0] = in_flight_[1] = false;
}

bool Sa11xxDmaStream::DoneCycle(uint64_t& cycle) const {
    return active_ && port_->CycleOfMoved(end_, cycle);
}

bool Sa11xxDmaStream::WordAddress(uint64_t word, uint32_t& pa) const {
    if (!active_ || word < origin_) return false;
    if (word < end_) {
        pa = Start(cur_b_) + static_cast<uint32_t>(word - origin_) * Datum();
        return true;
    }
    const bool other = !cur_b_;
    if (!in_flight_[other ? 1 : 0] || word - end_ >= Words(other)) return false;
    pa = Start(other) + static_cast<uint32_t>(word - end_) * Datum();
    return true;
}

void Sa11xxDmaStream::Save(StateWriter& w) const {
    w.Write<uint8_t>("stream_active", active_ ? 1u : 0u);
    w.Write<uint8_t>("stream_chained", chained_ ? 1u : 0u);
    w.Write<uint8_t>("stream_buffer_b", cur_b_ ? 1u : 0u);
    w.Write<uint64_t>("stream_origin", origin_);
    w.Write<uint64_t>("stream_end", end_);
    w.Write<uint32_t>("stream_pointer_moved_a", moved_[0]);
    w.Write<uint32_t>("stream_pointer_moved_b", moved_[1]);
    w.Write<uint32_t>("stream_pointer_written_a", written_[0]);
    w.Write<uint32_t>("stream_pointer_written_b", written_[1]);
    w.Write<uint32_t>("stream_latched_start_a", start_[0]);
    w.Write<uint32_t>("stream_latched_start_b", start_[1]);
    w.Write<uint32_t>("stream_latched_count_a", count_[0]);
    w.Write<uint32_t>("stream_latched_count_b", count_[1]);
    w.Write<uint8_t>("stream_in_flight_a", in_flight_[0] ? 1u : 0u);
    w.Write<uint8_t>("stream_in_flight_b", in_flight_[1] ? 1u : 0u);
    w.Write<uint8_t>("stream_announce", announce_ ? 1u : 0u);
}

void Sa11xxDmaStream::Restore(StateReader& r) {
    uint8_t active = 0, chained = 0, buffer_b = 0;
    r.Read("stream_active", active);
    r.Read("stream_chained", chained);
    r.Read("stream_buffer_b", buffer_b);
    r.Read("stream_origin", origin_);
    r.Read("stream_end", end_);
    r.Read("stream_pointer_moved_a", moved_[0]);
    r.Read("stream_pointer_moved_b", moved_[1]);
    r.Read("stream_pointer_written_a", written_[0]);
    r.Read("stream_pointer_written_b", written_[1]);
    r.Read("stream_latched_start_a", start_[0]);
    r.Read("stream_latched_start_b", start_[1]);
    r.Read("stream_latched_count_a", count_[0]);
    r.Read("stream_latched_count_b", count_[1]);
    uint8_t in_flight_a = 0, in_flight_b = 0;
    r.Read("stream_in_flight_a", in_flight_a);
    r.Read("stream_in_flight_b", in_flight_b);
    uint8_t announce = 0;
    r.Read("stream_announce", announce);
    in_flight_[0] = in_flight_a != 0u;
    in_flight_[1] = in_flight_b != 0u;
    announce_     = announce != 0u;
    active_  = active != 0u;
    chained_ = chained != 0u;
    cur_b_   = buffer_b != 0u;
}

}
