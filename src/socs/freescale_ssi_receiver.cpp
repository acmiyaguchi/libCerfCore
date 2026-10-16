#include "freescale_ssi_receiver.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_ssi_regs.h"

#include <algorithm>
#include <string>

namespace ssi = cerf_freescale_ssi;

void FreescaleSsiReceiver::Attach() {
    clock_ = &emu_.Get<GuestCycleClock>();
    line_.Attach();
}

void FreescaleSsiReceiver::Reset() {
    line_.Reset();
    dma_ = false;
    for (Channel& c : ch_) c = Channel{};
}

/* MCIMX51RM Table 56-20 RFWM0 (page 56-56): "The RFF0 flag is set whenever the data level in Rx
   FIFO 0 reaches the selected threshold." */
uint32_t FreescaleSsiReceiver::FullLevel(uint32_t k) const {
    return std::clamp(ch_[k].wm, 1u, depth_);
}

uint32_t FreescaleSsiReceiver::Period(uint32_t k) const {
    return std::min(ch_[k].burst, Threshold(k));
}

/* MCIMX31RM Table 45-11 RDMAE (page 45-39): "DMA requests are generated when any of the RFF0/1 bits
   in the SISR are set and if the corresponding RFEN bit is also set. If the corresponding FIFO is
   disabled, a DMA request is generated when the corresponding RDR bit is set." */
bool FreescaleSsiReceiver::DmaOn(uint32_t k) const {
    return line_.Enabled() && dma_ && (k == 0u || line_.TwoChannel()) && ch_[k].burst != 0u &&
           ch_[k].dma_clock;
}

void FreescaleSsiReceiver::ClearFifos() {
    for (Channel& c : ch_) {
        c.level   = 0u;
        c.overrun = false;
    }
}

void FreescaleSsiReceiver::Drain(uint32_t k) {
    Channel& c = ch_[k];
    if (!DmaOn(k) || c.level < Threshold(k)) return;
    const uint64_t bursts = (c.level - Threshold(k)) / c.burst + 1u;
    const uint64_t words  = bursts * c.burst;
    c.level = words >= c.level ? 0u : static_cast<uint32_t>(c.level - words);
    c.moved += words;
}

/* MCIMX51RM Table 56-13 (pages 56-44 / 56-45): ROE0 "set when the RXSR is filled and ready to
   transfer to SRX0 register or to Rx FIFO 0 (when enabled) and these are already full"; RFF0 "all
   further data received (for storage in this FIFO) is ignored until the FIFO contents are read". */
void FreescaleSsiReceiver::Land(uint32_t k, uint64_t n) {
    Channel& c = ch_[k];
    if (DmaOn(k)) {
        const uint64_t first = Threshold(k) - c.level;
        if (n < first) {
            c.level += static_cast<uint32_t>(n);
            return;
        }
        const uint64_t period = Period(k);
        const uint64_t rest   = n - first;
        c.level = static_cast<uint32_t>(Threshold(k) - period + rest % period);
        c.moved += (rest / period + 1u) * c.burst;
        return;
    }
    const uint32_t room = Depth(k) - c.level;
    if (n > room) {
        c.level   = Depth(k);
        c.overrun = true;
        return;
    }
    c.level += static_cast<uint32_t>(n);
}

uint64_t FreescaleSsiReceiver::CycleOfLanding(uint32_t k, uint64_t m) {
    const uint64_t slot = line_.NthUse(k, m);
    return slot == kNever ? kNever : line_.CycleOfEvent(slot);
}

/* MCIMX31RM §45.1.2.1.2 (page 45-7): "the receive shift register shifts in the received data
   available on the SRXD input and at the end of the time slot. This data is transferred to the
   receive data register." */
void FreescaleSsiReceiver::Settle() {
    if (!line_.Running()) return;
    const uint64_t           end = line_.EventsEnd();
    FreescaleSsiTimeline::Span s;
    while (line_.Next(end, s)) {
        for (uint32_t k = 0; k < 2u; ++k) {
            const uint64_t n = line_.Uses(k, s.ord, s.n);
            if (n != 0u) Land(k, n);
        }
    }
    line_.Finish();
}

void FreescaleSsiReceiver::Configure(const FreescaleSsiSectionControl& c,
                                     const FreescaleSsiFrameShape& shape) {
    Settle();
    dma_ = c.dma;
    for (uint32_t k = 0; k < 2u; ++k) {
        ch_[k].fen   = c.fen[k];
        ch_[k].wm    = c.wm[k];
        ch_[k].level = std::min(ch_[k].level, Depth(k));
    }
    line_.SetRoute(c.data, c.network, c.two_channel);
    if (!c.enabled) {
        ClearFifos();
        line_.Disable(c.run);
    } else if (!line_.Enabled()) {
        ClearFifos();
        line_.Enable(shape, c.run);
    } else {
        line_.Reshape(shape);
        if (c.run != line_.Run()) line_.SetRun(c.run);
    }
    for (uint32_t k = 0; k < 2u; ++k) Drain(k);
}

void FreescaleSsiReceiver::OnCpuRate() { line_.OnCpuRate(); }

/* MCIMX51RM Table 56-13 RDR0 (page 56-43): "RDR0 is cleared when the Core reads the SRX0 register.
   If Rx FIFO 0 is enabled, RDR0 is cleared when the FIFO is empty." */
void FreescaleSsiReceiver::ReadSrx(uint32_t k) {
    Settle();
    if (ch_[k].level != 0u) --ch_[k].level;
}

uint32_t FreescaleSsiReceiver::Level(uint32_t k) {
    Settle();
    return ch_[k].level;
}

void FreescaleSsiReceiver::ClearOverrun(uint32_t k) {
    Settle();
    ch_[k].overrun = false;
}

void FreescaleSsiReceiver::SetDmaBurst(uint32_t k, uint32_t words) {
    Settle();
    ch_[k].burst = words;
    ch_[k].moved = 0u;
    Drain(k);
}

void FreescaleSsiReceiver::SetDmaClock(uint32_t k, bool running) {
    Settle();
    ch_[k].dma_clock = running;
    Drain(k);
}

uint64_t FreescaleSsiReceiver::DmaWords(uint32_t k) {
    Settle();
    return ch_[k].moved;
}

bool FreescaleSsiReceiver::CycleOfDmaWords(uint32_t k, uint64_t words, uint64_t& cycle) {
    Settle();
    const Channel& c = ch_[k];
    if (words <= c.moved) {
        cycle = clock_->Cycles();
        return true;
    }
    if (!DmaOn(k) || !line_.Running()) return false;
    const uint64_t bursts = (words - c.moved + c.burst - 1u) / c.burst;
    const uint64_t at = CycleOfLanding(k, (Threshold(k) - c.level) + (bursts - 1u) * Period(k));
    if (at == kNever) return false;
    cycle = at;
    return true;
}

uint32_t FreescaleSsiReceiver::RxStatus() {
    Settle();
    uint32_t s = 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        const Channel& c = ch_[k];
        if (c.level != 0u) s |= ssi::Fifo(ssi::kSisrRdr0, k);
        if (c.fen && c.level >= FullLevel(k)) s |= ssi::Fifo(ssi::kSisrRff0, k);
        if (c.overrun) s |= ssi::Fifo(ssi::kSisrRoe0, k);
    }
    if (line_.FirstSlot()) s |= ssi::kSisrRfs;
    if (line_.LastSlot()) s |= ssi::kSisrRls;
    if (line_.FrameDone()) s |= ssi::kSisrRfrc;
    return s;
}

void FreescaleSsiReceiver::ClearLastSlot() {
    Settle();
    line_.ClearLastSlot();
}

uint64_t FreescaleSsiReceiver::NextStatusCycle(uint32_t watch) {
    Settle();
    if (!line_.Running()) return kNever;
    uint64_t at = kNever;
    for (uint32_t k = 0; k < 2u; ++k) {
        if (k == 1u && !line_.TwoChannel()) break;
        const Channel& c   = ch_[k];
        const bool     dma = DmaOn(k);
        if ((watch & ssi::Fifo(ssi::kSisrRdr0, k)) != 0u) {
            if (c.level == 0u && !(dma && Threshold(k) == 1u)) {
                at = std::min(at, CycleOfLanding(k, 1u));
            } else if (c.level != 0u && dma && Period(k) == Threshold(k)) {
                at = std::min(at, CycleOfLanding(k, Threshold(k) - c.level));
            }
        }
        if ((watch & ssi::Fifo(ssi::kSisrRff0, k)) != 0u && c.fen && !dma &&
            c.level < FullLevel(k)) {
            at = std::min(at, CycleOfLanding(k, FullLevel(k) - c.level));
        }
        if ((watch & ssi::Fifo(ssi::kSisrRoe0, k)) != 0u && !c.overrun && !dma) {
            at = std::min(at, CycleOfLanding(k, Depth(k) - c.level + 1u));
        }
    }
    return std::min(at, line_.NextStatusCycle((watch & ssi::kSisrRfs) != 0u,
                                              (watch & ssi::kSisrRls) != 0u,
                                              (watch & ssi::kSisrRfrc) != 0u));
}

void FreescaleSsiReceiver::Save(StateWriter& w) {
    Settle();
    w.Write<uint8_t>("rx_dma", dma_ ? 1u : 0u);
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        w.Write<uint8_t>(("rx_fen" + n).c_str(), ch_[k].fen ? 1u : 0u);
        w.Write<uint8_t>(("rx_overrun" + n).c_str(), ch_[k].overrun ? 1u : 0u);
        w.Write<uint32_t>(("rx_wm" + n).c_str(), ch_[k].wm);
        w.Write<uint32_t>(("rx_level" + n).c_str(), ch_[k].level);
        w.Write<uint32_t>(("rx_burst" + n).c_str(), ch_[k].burst);
        w.Write<uint64_t>(("rx_dma_words" + n).c_str(), ch_[k].moved);
        w.Write<uint8_t>(("rx_dma_clock" + n).c_str(), ch_[k].dma_clock ? 1u : 0u);
    }
    line_.Save(w, "rx_");
}

void FreescaleSsiReceiver::Restore(StateReader& r) {
    uint8_t dma = 0;
    r.Read("rx_dma", dma);
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        uint8_t fen = 0, overrun = 0, dma_clock = 0;
        r.Read(("rx_fen" + n).c_str(), fen);
        r.Read(("rx_overrun" + n).c_str(), overrun);
        r.Read(("rx_wm" + n).c_str(), ch_[k].wm);
        r.Read(("rx_level" + n).c_str(), ch_[k].level);
        r.Read(("rx_burst" + n).c_str(), ch_[k].burst);
        r.Read(("rx_dma_words" + n).c_str(), ch_[k].moved);
        r.Read(("rx_dma_clock" + n).c_str(), dma_clock);
        ch_[k].fen       = fen != 0u;
        ch_[k].overrun   = overrun != 0u;
        ch_[k].dma_clock = dma_clock != 0u;
    }
    line_.Restore(r, "rx_");
    dma_ = dma != 0u;
}
