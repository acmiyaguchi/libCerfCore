#include "sa1111_sac_tx_stream.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa1111_sac_frame_clock.h"

#include <algorithm>

bool Sa1111SacTxStream::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacTxStream::OnReady() {
    frames_ = &emu_.Get<Sa1111SacFrameClock>();
}

bool Sa1111SacTxStream::Rescale(uint64_t now, uint64_t cpu_hz, GuestCycleClock::Rate fs) {
    Settle(now);
    start_ = std::max(start_, Consumed(now));
    return frames_->Rescale(now, cpu_hz, fs);
}

void Sa1111SacTxStream::StopClock(uint64_t now) {
    Settle(now);
    frames_->Stop(now);
}

uint64_t Sa1111SacTxStream::Consumed(uint64_t now) const {
    return running_ ? frames_->Position(now) - skip_ : held_;
}

uint64_t Sa1111SacTxStream::ConsumeCycle(uint64_t words) const {
    return frames_->CycleOfPosition(words + skip_);
}

/* §7.2.2: "the SAC requests the bus using DMA_Req<1>" when data is to be transferred
   "(generally in response to a Transmit or Receive FIFO condition)". */
uint64_t Sa1111SacTxStream::RequestCycle(uint64_t at) const {
    if (!running_) return start_cycle_;
    return std::max(ConsumeCycle(at), start_cycle_);
}

/* SA-1111 Developer's Manual Table 7-10 TUR: "1 - Attempted data write to full Transmit FIFO". */
uint32_t Sa1111SacTxStream::LostLanded(uint64_t now) const {
    return last_.issue <= now ? last_.lost & ~lost_seen_ : 0u;
}

uint64_t Sa1111SacTxStream::LandedCycle() const {
    return Words(last_) == 0u ? 0u : last_.issue;
}

bool Sa1111SacTxStream::InFlightGap(uint64_t now) const {
    if (!running_ || resolved_ || Words(last_) == 0u) return false;
    const uint64_t consume = ConsumeCycle(last_base_ + 1u);
    return consume <= now && consume < last_.issue;
}

/* SA-1111 Developer's Manual §7.3.2.2: "Each phase of the Left/Right signal is accompanied
   by one serial audio data sample on the data pins SDATA_IN and SDATA_OUT". */
void Sa1111SacTxStream::Run(uint64_t now) {
    skip_    = frames_->Position(now) - held_;
    running_ = true;
}

void Sa1111SacTxStream::Hold(uint64_t now) {
    Settle(now);
    held_    = frames_->Position(now) - skip_;
    running_ = false;
}

/* Table 7-10 TUR: "0 - Transmit FIFO has not experienced an under-run"; Table 11-1: 38
   AudTUR "Audio Transmit FIFO under-run interrupt". */
void Sa1111SacTxStream::ResolveGap(uint64_t now) {
    while (InFlightGap(now)) {
        underrun_ = true;
        ++skip_;
    }
    if (Words(last_) != 0u && last_.issue <= now) resolved_ = true;
}

void Sa1111SacTxStream::Settle(uint64_t now) {
    ResolveGap(now);
    const uint32_t lost = LostLanded(now);
    if (lost != 0u) {
        underrun_    = true;
        lost_seen_  |= lost;
    }
    const uint64_t pos = Consumed(now);
    if (pushed_ >= pos) return;
    underrun_ = true;
    pushed_   = pos;
}

bool Sa1111SacTxStream::Underrun(uint64_t now) const {
    return underrun_ || InFlightGap(now) || pushed_ < Consumed(now) || LostLanded(now) != 0u;
}

void Sa1111SacTxStream::StartBlock(uint64_t now) {
    Settle(now);
    start_           = Consumed(now);
    start_cycle_     = now;
    stalled_request_ = false;
}

void Sa1111SacTxStream::Clear(uint64_t now) {
    pushed_          = Consumed(now);
    start_           = pushed_;
    last_            = Request{};
    lost_seen_       = 0u;
    last_base_       = pushed_;
    underrun_        = false;
    stalled_request_ = false;
}

/* SA-1111 Developer's Manual §3.2.3.1 (printed 3-9): "The SBI asserts MBREQ to the SA-1110
   processor. When the system bus is idle, the processor responds with MBGNT". */
void Sa1111SacTxStream::SetGrant(uint64_t now, Sa1111Sbi::BusGrant grant) {
    using Grant = Sa1111Sbi::BusGrant;
    if (grant == grant_) return;
    const Grant old     = grant_;
    const bool  waiting = stalled_request_;
    grant_ = grant;
    if (grant == Grant::Granted) {
        if (waiting) start_cycle_ = std::max(start_cycle_, now);
        stalled_request_ = false;
    }
    LOG(Periph, "[Sa1111Sac] DMA bus grant %d -> %d at cyc=%llu (waiting=%d)\n",
        static_cast<int>(old), static_cast<int>(grant), static_cast<unsigned long long>(now),
        waiting ? 1 : 0);
}

/* Table 7-10 TFS: "Transmit FIFO level is at or below TFL threshold"; §7.2.2: "the SAC requests
   the bus using DMA_Req<1>". */
bool Sa1111SacTxStream::RequestDue(uint64_t now, uint32_t threshold) const {
    uint64_t at = pushed_ > threshold ? pushed_ - threshold : 0u;
    if (at < start_) at = start_;
    return at <= Consumed(now);
}

/* Table 7-12 SASCR TUR: "Clears Transmit FIFO under-run status bit". */
void Sa1111SacTxStream::ClearUnderrun(uint64_t now) {
    Settle(now);
    underrun_ = false;
}

/* §7.4.22 (printed 7-21): SADR "transfers data to the Transmit FIFO"; Table 7-10 TUR: "1 -
   Attempted data write to full Transmit FIFO". */
bool Sa1111SacTxStream::PushWord(uint64_t now) {
    Settle(now);
    if (Level(now) >= kFifoDepth) {
        underrun_ = true;
        return false;
    }
    ++pushed_;
    return true;
}

uint32_t Sa1111SacTxStream::Level(uint64_t now) const {
    const uint64_t pos = Consumed(now);
    return pushed_ < pos ? 0u : static_cast<uint32_t>(pushed_ - pos);
}

/* SA-1111 Developer's Manual Table 7-10 TFS: "0 - Transmit FIFO level exceeds TFL threshold,
   or SAC disabled". */
bool Sa1111SacTxStream::ServiceRequest(uint64_t now, bool enabled, uint32_t threshold) const {
    return enabled && Level(now) <= threshold;
}

/* Table 7-10: TNF bit 0, TFS bit 3, TUR bit 5, TFL 11:8 "Number of entries in Transmit
   FIFO". */
uint32_t Sa1111SacTxStream::StatusBits(uint64_t now, bool enabled, uint32_t threshold) const {
    const uint32_t level = Level(now);
    uint32_t bits = (level & 0xFu) << 8;
    if (level < kFifoDepth)                        bits |= 1u << 0;
    if (ServiceRequest(now, enabled, threshold))   bits |= 1u << 3;
    if (Underrun(now))                             bits |= 1u << 5;
    return bits;
}

/* SA-1111 Developer's Manual Table 7-10 TFS: "Transmit FIFO level is at or below TFL
   threshold"; Table 7-7 TFTH: "Latency values of nine or greater have their bursts separated
   into two bursts"; §7.2.2: "After a transfer burst, the SAC must give up its request". */
bool Sa1111SacTxStream::Step(uint32_t threshold, uint32_t& words_left, uint64_t limit,
                             uint64_t now, bool bounded, StepState& s,
                             std::vector<Request>* plan) const {
    while (words_left != 0u) {
        uint64_t at = s.pushed > threshold ? s.pushed - threshold : 0u;
        if (at < s.start) at = s.start;
        if (limit < at) return false;
        Request rq;
        rq.issue = RequestCycle(at);
        if (Words(s.last) != 0u) rq.issue = std::max(rq.issue, s.last.issue + kBusHandBackCycles);
        if (bounded && rq.issue > now) return false;
        ++s.requests;
        const uint32_t count = std::min(threshold, words_left);
        rq.burst[0] = std::min(kMaxBurst, count);
        rq.burst[1] = count - rq.burst[0];
        if (rq.burst[1] > kMaxBurst) {
            emu_.Get<Fatal>().Die("Sa1111SacTxStream: a third burst in one DMA request "
                                  "(threshold %u)", threshold);
        }
        /* Table 7-7 TFTH: "Using threshold values that are too large can result in an overflow
           condition"; §7.4.1.1: "the FIFOs, which are 16 levels deep". */
        const uint64_t taken = Consumed(rq.issue);
        uint32_t       kept  = 0u;
        for (uint32_t word = 1u; word <= count; ++word) {
            const uint64_t held = s.pushed + kept;
            if (held <= taken || held - taken < kFifoDepth) ++kept;
            else                                           rq.lost |= 1u << (word - 1u);
        }
        if ((s.last.lost & ~s.lost_seen) != 0u) s.overflowed = true;
        s.lost_seen  = 0u;
        s.last_base  = s.pushed;
        s.pushed    += kept;
        words_left  -= count;
        s.last       = rq;
        s.start      = at;
        if (plan != nullptr) plan->push_back(rq);
    }
    return true;
}

void Sa1111SacTxStream::Landings(uint64_t now, uint32_t threshold, uint32_t words_left,
                                 std::vector<Landing>& out) const {
    std::vector<Request> plan;
    if (words_left != 0u && grant_ == Sa1111Sbi::BusGrant::Granted) {
        StepState s;
        s.pushed = pushed_;
        s.start  = start_;
        s.last   = last_;
        Step(threshold, words_left, running_ ? UINT64_MAX : Consumed(now), 0u, false, s, &plan);
    }
    plan.insert(plan.begin(), last_);
    for (const Request& rq : plan) {
        if (rq.issue <= now) continue;
        for (uint32_t word = 1u; word <= Words(rq); ++word) {
            out.push_back(Landing{rq.issue, (rq.lost & (1u << (word - 1u))) == 0u});
        }
    }
}

/* SA-1111 Developer's Manual Table 7-10 TFS: "1 - Transmit FIFO level is at or below TFL
   threshold (Interruptible)"; Table 11-1: 36 AudTFSR. */
bool Sa1111SacTxStream::NextServiceEdge(uint64_t now, uint32_t threshold, uint32_t words_left,
                                        bool rising, uint64_t& cycle) const {
    std::vector<Landing> lands;
    Landings(now, threshold, std::min(words_left, 2u * threshold), lands);
    uint64_t landed = pushed_;
    uint64_t from   = now;
    bool     high   = Level(now) <= threshold;
    for (size_t k = 0; k <= lands.size(); ++k) {
        const uint64_t until = k < lands.size() ? lands[k].cycle : UINT64_MAX;
        if (!high && running_) {
            const uint64_t c = std::max(ConsumeCycle(landed - threshold), from);
            if (c < until) {
                if (rising) {
                    cycle = c;
                    return true;
                }
                high = true;
            }
        }
        if (k == lands.size()) break;
        from = until;
        if (!lands[k].kept) continue;
        ++landed;
        const uint64_t taken = Consumed(until);
        if (high && landed > taken && landed - taken > threshold) {
            if (!rising) {
                cycle = until;
                return true;
            }
            high = false;
        }
    }
    return false;
}

/* Table 7-10 TUR: "0 - Transmit FIFO has not experienced an under-run", "1 - Attempted data
   write to full Transmit FIFO"; Table 11-1: 38 AudTUR. */
bool Sa1111SacTxStream::NextUnderrun(uint64_t now, uint32_t threshold, uint32_t words_left,
                                     uint64_t& cycle) const {
    if (Underrun(now)) return false;
    std::vector<Landing> lands;
    Landings(now, threshold, words_left, lands);
    uint64_t landed = pushed_;
    uint64_t from   = now;
    for (size_t k = 0; k <= lands.size(); ++k) {
        const uint64_t until = k < lands.size() ? lands[k].cycle : UINT64_MAX;
        if (running_) {
            const uint64_t c = std::max(ConsumeCycle(landed + 1u), from);
            if (c < until) {
                cycle = c;
                return true;
            }
        }
        if (k == lands.size()) break;
        if (!lands[k].kept) {
            cycle = until;
            return true;
        }
        ++landed;
        from = until;
    }
    return false;
}

bool Sa1111SacTxStream::Fill(uint64_t now, uint32_t threshold, uint32_t& words_left) {
    ResolveGap(now);
    if (grant_ != Sa1111Sbi::BusGrant::Granted) {
        if (words_left != 0u && RequestDue(now, threshold)) stalled_request_ = true;
        return false;
    }
    StepState s;
    s.pushed    = pushed_;
    s.start     = start_;
    s.requests  = requests_;
    s.last_base = last_base_;
    s.lost_seen = lost_seen_;
    s.last      = last_;
    const uint64_t before = s.requests;
    const bool done = Step(threshold, words_left, Consumed(now), now, true, s);
    if (s.requests != before) {
        resolved_  = false;
        lost_seen_ = 0u;
    }
    if (s.overflowed) underrun_ = true;
    pushed_    = s.pushed;
    start_     = s.start;
    requests_  = s.requests;
    last_base_ = s.last_base;
    last_      = s.last;
    return done;
}

bool Sa1111SacTxStream::DoneCycle(uint32_t threshold, uint32_t words_left,
                                  uint64_t& cycle) const {
    if (!running_ || grant_ != Sa1111Sbi::BusGrant::Granted) return false;
    StepState s;
    s.pushed = pushed_;
    s.start  = start_;
    s.last   = last_;
    if (!Step(threshold, words_left, UINT64_MAX, 0u, false, s)) return false;
    cycle = s.last.issue;
    return true;
}

bool Sa1111SacTxStream::NextRequestCycle(uint32_t threshold, uint32_t words_left,
                                         uint64_t& cycle) const {
    return DoneCycle(threshold, std::min(threshold, words_left), cycle);
}

void Sa1111SacTxStream::Save(StateWriter& w, uint64_t now) const {
    w.Write<uint32_t>("tx_serializer_running", running_ ? 1u : 0u);
    w.Write<uint32_t>("tx_fifo_level", Level(now));
    w.Write<uint32_t>("tx_underrun", Underrun(now) ? 1u : 0u);
}

void Sa1111SacTxStream::Restore(StateReader& r, uint64_t now) {
    uint32_t running = 0u, level = 0u, underrun = 0u;
    r.Read("tx_serializer_running", running);
    r.Read("tx_fifo_level", level);
    r.Read("tx_underrun", underrun);
    held_            = 0u;
    skip_            = 0u;
    running_         = running != 0u;
    start_           = 0u;
    start_cycle_     = now;
    last_            = Request{};
    lost_seen_       = 0u;
    pushed_          = level;
    last_base_       = pushed_;
    resolved_        = true;
    underrun_        = underrun != 0u;
    stalled_request_ = false;
}

REGISTER_SERVICE(Sa1111SacTxStream);
