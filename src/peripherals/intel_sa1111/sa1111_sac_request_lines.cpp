#include "sa1111_sac_request_lines.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "sa1111_intc.h"
#include "sa1111_sac_frame_clock.h"
#include "sa1111_sac_l3.h"
#include "sa1111_sac_rx_fifo.h"
#include "sa1111_sac_tx_stream.h"

bool Sa1111SacRequestLines::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacRequestLines::OnReady() {
    intc_    = &emu_.Get<Sa1111Intc>();
    stream_  = &emu_.Get<Sa1111SacTxStream>();
    frames_  = &emu_.Get<Sa1111SacFrameClock>();
    rx_      = &emu_.Get<Sa1111SacRxFifo>();
    l3_      = &emu_.Get<Sa1111SacL3>();
    clock_   = &emu_.Get<GuestCycleClock>();
    edge_ev_ = clock_->Add([this] { on_edge_(clock_->Cycles()); });
}

void Sa1111SacRequestLines::SetEdgeHandler(std::function<void(uint64_t now)> on_edge) {
    on_edge_ = std::move(on_edge);
}

/* SA-1111 Developer's Manual Figure 11-1 (printed 11-2): IntLatched(n) is clocked by
   (IntRaw(n) XOR IntPol(n)) AND ~Sleep AND IntEn(n), or by WakeupEN AND Sleep AND
   (IntRaw(n) XOR WakeupPol(n)). */
bool Sa1111SacRequestLines::NextEdge(uint64_t now, const Inputs& in, uint64_t& cycle) {
    const uint32_t enables = intc_->ClockedSources1();
    if ((enables & kSampledSources1) == 0u) return false;
    const uint32_t inverted = intc_->ClockPolarity1();
    const auto armed  = [&](uint8_t src) { return (enables & (1u << (src - 32u))) != 0u; };
    const auto rising = [&](uint8_t src) { return (inverted & (1u << (src - 32u))) == 0u; };
    bool     found = false;
    uint64_t c     = 0;
    const auto take = [&](bool hit) {
        if (!hit || c <= now || (found && c >= cycle)) return;
        cycle = c;
        found = true;
    };
    if (armed(kSrcTfsr) && in.enabled) {
        take(stream_->NextServiceEdge(now, in.tx_threshold, in.tx_words_left, rising(kSrcTfsr), c));
        if (in.tx_draining) {
            c = in.tx_next_request;
            take(true);
        }
    }
    if (armed(kSrcTur) && rising(kSrcTur)) {
        take(stream_->NextUnderrun(now, in.tx_threshold, in.tx_words_left, c));
    }
    if (in.rx_draining) {
        c = in.rx_next_request;
        take(armed(kSrcRfsr));
        return found;
    }
    const uint64_t pos = frames_->Position(now);
    uint64_t at = 0;
    if (armed(kSrcRfsr) && rising(kSrcRfsr) && in.enabled &&
        rx_->NextReach(pos, in.rx_threshold, at)) {
        c = frames_->CycleOfPosition(at);
        take(true);
    }
    if (armed(kSrcRor) && rising(kSrcRor) && !rx_->Overrun(pos) &&
        rx_->NextReach(pos, Sa1111SacTxStream::kFifoDepth + 1u, at)) {
        c = frames_->CycleOfPosition(at);
        take(true);
    }
    return found;
}

void Sa1111SacRequestLines::RefreshEdges(uint64_t now, const Inputs& in) {
    uint64_t cycle = 0;
    if (NextEdge(now, in, cycle)) {
        clock_->Arm(edge_ev_, cycle);
    } else {
        clock_->Disarm(edge_ev_);
    }
}

Sa1111SacRequestLines::Levels Sa1111SacRequestLines::Sample(uint64_t now,
                                                            const Inputs& in) const {
    const uint64_t pos = frames_->Position(now);
    Levels levels;
    levels.tfs         = stream_->ServiceRequest(now, in.enabled, in.tx_threshold);
    levels.requests    = stream_->Requests();
    levels.rfs         = rx_->ServiceRequest(pos, in.enabled, in.rx_threshold);
    levels.rx_requests = in.rx_requests;
    levels.tur         = stream_->Underrun(now);
    levels.ror         = rx_->Overrun(pos);
    levels.dts         = l3_->DataSent();
    levels.rdd         = l3_->DataRead();
    return levels;
}

/* SA-1111 Developer's Manual §7.4.21 (printed 7-21): SAITR "generates a interrupt strobe signal
   to Interrupt Controller"; its bits 8-11 are TDBDA, TDBDB, RDBDA and RDBDB (Table 7-29). */
void Sa1111SacRequestLines::StrobeDone(uint8_t source) {
    intc_->StrobeSource(source);
}

void Sa1111SacRequestLines::Baseline(uint64_t now, const Inputs& in) {
    pub_ = Sample(now, in);
}

/* SA-1111 Developer's Manual Table 7-10: TFS "1 - Transmit FIFO level is at or below TFL
   threshold (Interruptible)", RFS "1 - Receive FIFO level is at or above RFL threshold
   (Interruptible)"; §7.2.2: "After a transfer burst, the SAC must give up its request". */
void Sa1111SacRequestLines::PulseRequests(uint8_t source, uint64_t requests, bool& published) {
    if (requests == 0u) return;
    if (!published) intc_->SetSourceLevel(source, true);
    intc_->SetSourceLevel(source, false);
    if (requests > 1u) {
        intc_->SetSourceLevel(source, true);
        intc_->SetSourceLevel(source, false);
    }
    published = false;
}

void Sa1111SacRequestLines::Publish(uint64_t now, const Inputs& in) {
    const Levels next = Sample(now, in);
    PulseRequests(kSrcTfsr, next.requests - pub_.requests, pub_.tfs);
    PulseRequests(kSrcRfsr, next.rx_requests - pub_.rx_requests, pub_.rfs);
    if (next.tfs != pub_.tfs) intc_->SetSourceLevel(kSrcTfsr, next.tfs);
    if (next.rfs != pub_.rfs) intc_->SetSourceLevel(kSrcRfsr, next.rfs);
    if (next.tur != pub_.tur) intc_->SetSourceLevel(kSrcTur,  next.tur);
    if (next.ror != pub_.ror) intc_->SetSourceLevel(kSrcRor,  next.ror);
    if (next.dts != pub_.dts) intc_->SetSourceLevel(kSrcDts,  next.dts);
    if (next.rdd != pub_.rdd) intc_->SetSourceLevel(kSrcRdd,  next.rdd);
    pub_ = next;
}

REGISTER_SERVICE(Sa1111SacRequestLines);
