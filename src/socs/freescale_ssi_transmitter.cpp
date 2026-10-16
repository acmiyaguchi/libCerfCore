#include "freescale_ssi_transmitter.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_ssi_regs.h"

#include <algorithm>
#include <string>

namespace cerfcore {

namespace ssi = cerf_freescale_ssi;

void FreescaleSsiTransmitter::Attach() {
    clock_ = &emu_.Get<GuestCycleClock>();
    line_.Attach();
}

void FreescaleSsiTransmitter::Reset() {
    line_.Reset();
    dma_ = command_frames_ = cmd_pending_ = false;
    cmd_from_ = 0u;
    for (Channel& c : ch_) {
        c.fen      = false;
        c.wm       = 0u;
        c.underrun = false;
        c.fifo.Configure(1u, 0u, 0u);
        c.fifo.Clear();
        c.fifo.ResetMoved();
    }
    ConfigureFifos();
    UpdateSupply();
}

/* MCIMX51RM Table 56-20 TFWM1 (page 56-56): "The TFE1 flag is set whenever the empty slots in Tx
   FIFO exceed or are equal to the selected threshold." */
uint32_t FreescaleSsiTransmitter::EmptyLevel(uint32_t k) const {
    return depth_ - std::clamp(ch_[k].wm, 1u, depth_);
}

/* MCIMX31RM Table 45-11 TDMAE (page 45-39): "DMA requests are generated when any of the TFE0/1 bits
   in the SISR are set and if the corresponding TFEN bit is also set. If the corresponding FIFO is
   disabled, a DMA request is generated when the corresponding TDE bit is set." */
bool FreescaleSsiTransmitter::DmaOn(uint32_t k) const {
    return line_.Enabled() && dma_ && (k == 0u || line_.TwoChannel()) &&
           ch_[k].fifo.Burst() != 0u && ch_[k].dma_clock;
}

/* MCIMX31RM Table 45-9 SSIEN (page 45-34): "the contents of transmit and receive FIFOs are
   cleared". */
void FreescaleSsiTransmitter::ClearFifos() {
    for (Channel& c : ch_) {
        c.fifo.Clear();
        c.underrun = false;
    }
}

void FreescaleSsiTransmitter::ConfigureFifos() {
    for (uint32_t k = 0; k < 2u; ++k) {
        DmaBurstFifo&  f     = ch_[k].fifo;
        const uint32_t level = std::min(f.Level(), Depth(k));
        f.Configure(Depth(k), Threshold(k), f.Burst());
        f.Restore(level, f.Moved());
    }
}

void FreescaleSsiTransmitter::UpdateSupply() {
    for (uint32_t k = 0; k < 2u; ++k) {
        ch_[k].fifo.SetSupply(DmaOn(k) ? DmaBurstFifo::kUnlimited : 0u);
    }
}

void FreescaleSsiTransmitter::Refill() {
    for (Channel& c : ch_) c.fifo.Refill();
}

uint64_t FreescaleSsiTransmitter::CycleOfTake(uint32_t k, uint64_t m) {
    const uint64_t slot = line_.NthUse(k, m);
    return slot == kNever ? kNever : line_.CycleOfEvent(slot);
}

/* MCIMX31RM Table 45-7 (page 45-26): data written to STX "is transferred to the transmit shift
   register (TXSR), when shifting of the previous data is complete". */
void FreescaleSsiTransmitter::Settle() {
    if (!line_.Running()) return;
    const uint64_t           end = line_.EventsEnd();
    FreescaleSsiTimeline::Span s;
    while (line_.Next(end, s)) {
        if (cmd_pending_ && command_frames_ && line_.FrameEnded(cmd_from_, s.to)) {
            cmd_pending_ = false;
        }
        for (uint32_t k = 0; k < 2u; ++k) {
            const uint64_t t = line_.Uses(k, s.ord, s.n);
            if (t != 0u && ch_[k].fifo.Take(t) != 0u) ch_[k].underrun = true;
        }
    }
    line_.Finish();
    Refill();
}

void FreescaleSsiTransmitter::Configure(const FreescaleSsiSectionControl& c,
                                        const FreescaleSsiFrameShape& shape, bool command_frames) {
    Settle();
    dma_            = c.dma;
    command_frames_ = command_frames;
    for (uint32_t k = 0; k < 2u; ++k) {
        ch_[k].fen = c.fen[k];
        ch_[k].wm  = c.wm[k];
    }
    line_.SetRoute(c.data, c.network, c.two_channel);
    if (!c.enabled) {
        ClearFifos();
        line_.Disable(c.run);
        cmd_from_ = 0u;
    } else if (!line_.Enabled()) {
        ClearFifos();
        line_.Enable(shape, c.run);
        cmd_from_ = 0u;
    } else {
        if (line_.Reshape(shape)) cmd_from_ = 0u;
        if (c.run != line_.Run()) line_.SetRun(c.run);
    }
    ConfigureFifos();
    UpdateSupply();
    Refill();
}

void FreescaleSsiTransmitter::OnCpuRate() { line_.OnCpuRate(); }

/* MCIMX51RM Table 56-10 (page 56-33): "If Tx FIFO0 is not in use and user writes Data1, Data2 to
   STX0, then Data2 will not over-write Data1 and will be discarded"; NOTE: "Enable SSI (SSIEN=1)
   before writing to SSI Transmit Data Registers." */
void FreescaleSsiTransmitter::WriteStx(uint32_t k) {
    if (!line_.Enabled()) return;
    Settle();
    ch_[k].fifo.Put();
}

/* MCIMX51RM Table 56-22 WR (page 56-61): "the next frame will carry an AC97 Write Command", "This
   bit is automatically cleared by the SSI after completing transmission of a frame." */
void FreescaleSsiTransmitter::SetCommand(bool pending) {
    Settle();
    cmd_pending_ = pending;
    cmd_from_    = line_.NextFrameStart();
}

bool FreescaleSsiTransmitter::CommandPending() {
    Settle();
    return cmd_pending_;
}

void FreescaleSsiTransmitter::SetDmaBurst(uint32_t k, uint32_t words) {
    Settle();
    ch_[k].fifo.Configure(Depth(k), Threshold(k), words);
    ch_[k].fifo.ResetMoved();
    UpdateSupply();
    Refill();
}

void FreescaleSsiTransmitter::SetDmaClock(uint32_t k, bool running) {
    Settle();
    ch_[k].dma_clock = running;
    UpdateSupply();
    Refill();
}

uint32_t FreescaleSsiTransmitter::Level(uint32_t k) {
    Settle();
    return ch_[k].fifo.Level();
}

void FreescaleSsiTransmitter::ClearUnderrun(uint32_t k) {
    Settle();
    ch_[k].underrun = false;
}

/* MCIMX51RM Table 56-13: TFE0 "set when the empty slots in Tx FIFO exceed or are equal to the
   selected Tx FIFO WaterMark 0"; TDE0 "occurs when there is at least one empty slot in STX0 or Tx
   FIFO 0"; Figure 56-28 TFE1 and TDE1 reset 1. */
uint32_t FreescaleSsiTransmitter::TxStatus() {
    Settle();
    uint32_t s = 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        const uint32_t level = ch_[k].fifo.Level();
        if (level <= EmptyLevel(k)) s |= ssi::Fifo(ssi::kSisrTfe0, k);
        if (level < Depth(k)) s |= ssi::Fifo(ssi::kSisrTde0, k);
        if (ch_[k].underrun) s |= ssi::Fifo(ssi::kSisrTue0, k);
    }
    if (line_.FirstSlot()) s |= ssi::kSisrTfs;
    if (line_.LastSlot()) s |= ssi::kSisrTls;
    if (line_.FrameDone()) s |= ssi::kSisrTfrc;
    return s;
}

void FreescaleSsiTransmitter::ClearLastSlot() {
    Settle();
    line_.ClearLastSlot();
}

uint64_t FreescaleSsiTransmitter::NextStatusCycle(uint32_t watch) {
    Settle();
    if (!line_.Running()) return kNever;
    uint64_t at = kNever;
    for (uint32_t k = 0; k < 2u; ++k) {
        if (k == 1u && !line_.TwoChannel()) break;
        const Channel& c     = ch_[k];
        const uint64_t left  = c.fifo.TakesBeforeEmpty();
        const uint32_t level = c.fifo.Level();
        const bool     fed   = left == DmaBurstFifo::kUnlimited;
        if ((watch & ssi::Fifo(ssi::kSisrTue0, k)) != 0u && !c.underrun && !fed) {
            at = std::min(at, CycleOfTake(k, left + 1u));
        }
        if ((watch & ssi::Fifo(ssi::kSisrTfe0, k)) != 0u && level > EmptyLevel(k) && !fed) {
            at = std::min(at, CycleOfTake(k, left - EmptyLevel(k)));
        }
        if ((watch & ssi::Fifo(ssi::kSisrTde0, k)) != 0u && level >= Depth(k)) {
            at = std::min(at, CycleOfTake(k, 1u));
        }
    }
    return std::min(at, line_.NextStatusCycle((watch & ssi::kSisrTfs) != 0u,
                                              (watch & ssi::kSisrTls) != 0u,
                                              (watch & ssi::kSisrTfrc) != 0u));
}

uint64_t FreescaleSsiTransmitter::DmaWords(uint32_t k) {
    Settle();
    return ch_[k].fifo.Moved();
}

bool FreescaleSsiTransmitter::CycleOfDmaWords(uint32_t k, uint64_t words, uint64_t& cycle) {
    Settle();
    const DmaBurstFifo& f = ch_[k].fifo;
    if (words <= f.Moved()) {
        cycle = clock_->Cycles();
        return true;
    }
    if (!DmaOn(k) || !line_.Running()) return false;
    uint64_t takes = 0;
    if (!f.TakesToMove(words, takes)) return false;
    const uint64_t at = CycleOfTake(k, takes);
    if (at == kNever) return false;
    cycle = at;
    return true;
}

void FreescaleSsiTransmitter::Save(StateWriter& w) {
    Settle();
    w.Write<uint8_t>("tx_dma", dma_ ? 1u : 0u);
    w.Write<uint8_t>("tx_command_frames", command_frames_ ? 1u : 0u);
    w.Write<uint8_t>("tx_cmd_pending", cmd_pending_ ? 1u : 0u);
    w.Write<uint64_t>("tx_cmd_from", cmd_from_);
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        w.Write<uint8_t>(("tx_fen" + n).c_str(), ch_[k].fen ? 1u : 0u);
        w.Write<uint8_t>(("tx_underrun" + n).c_str(), ch_[k].underrun ? 1u : 0u);
        w.Write<uint32_t>(("tx_wm" + n).c_str(), ch_[k].wm);
        w.Write<uint32_t>(("tx_level" + n).c_str(), ch_[k].fifo.Level());
        w.Write<uint32_t>(("tx_burst" + n).c_str(), ch_[k].fifo.Burst());
        w.Write<uint64_t>(("tx_dma_words" + n).c_str(), ch_[k].fifo.Moved());
        w.Write<uint8_t>(("tx_dma_clock" + n).c_str(), ch_[k].dma_clock ? 1u : 0u);
    }
    line_.Save(w, "tx_");
}

void FreescaleSsiTransmitter::Restore(StateReader& r) {
    uint8_t dma = 0, command_frames = 0, cmd_pending = 0;
    r.Read("tx_dma", dma);
    r.Read("tx_command_frames", command_frames);
    r.Read("tx_cmd_pending", cmd_pending);
    r.Read("tx_cmd_from", cmd_from_);
    uint32_t level[2] = {}, burst[2] = {};
    uint64_t moved[2] = {};
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        uint8_t fen = 0, underrun = 0, dma_clock = 0;
        r.Read(("tx_fen" + n).c_str(), fen);
        r.Read(("tx_underrun" + n).c_str(), underrun);
        r.Read(("tx_wm" + n).c_str(), ch_[k].wm);
        r.Read(("tx_level" + n).c_str(), level[k]);
        r.Read(("tx_burst" + n).c_str(), burst[k]);
        r.Read(("tx_dma_words" + n).c_str(), moved[k]);
        r.Read(("tx_dma_clock" + n).c_str(), dma_clock);
        ch_[k].fen       = fen != 0u;
        ch_[k].underrun  = underrun != 0u;
        ch_[k].dma_clock = dma_clock != 0u;
    }
    line_.Restore(r, "tx_");
    dma_            = dma != 0u;
    command_frames_ = command_frames != 0u;
    cmd_pending_    = cmd_pending != 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        ch_[k].fifo.Configure(Depth(k), Threshold(k), burst[k]);
        ch_[k].fifo.Restore(level[k], moved[k]);
    }
    UpdateSupply();
}

}
