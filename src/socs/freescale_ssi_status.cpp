#include "freescale_ssi_status.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_ssi_receiver.h"
#include "freescale_ssi_regs.h"
#include "freescale_ssi_transmitter.h"

#include <algorithm>
#include <string>

namespace cerfcore {

namespace ssi = cerf_freescale_ssi;

void FreescaleSsiStatus::Attach(std::function<void(bool)> line) {
    clock_ = &emu_.Get<GuestCycleClock>();
    ev_    = clock_->Add([this] { Update(); });
    line_  = std::move(line);
}

void FreescaleSsiStatus::Reset() {
    for (uint32_t k = 0; k < 2u; ++k) tue_read_[k] = roe_read_[k] = false;
}

void FreescaleSsiStatus::SetControl(const FreescaleSsiStatusControl& c) {
    ctl_ = c;
    Update();
}

/* MCIMX31RM Table 45-10 (page 45-37): TFS and RFS "In normal mode, this bit is always high";
   MCIMX51RM Table 56-13 RFS (page 56-44): "In Normal mode, this bit is always high." */
uint32_t FreescaleSsiStatus::Forced() const {
    if (!ctl_.enabled || ctl_.network) return 0u;
    return ssi::kSisrRfs | (rules_.tfs_high_in_normal ? ssi::kSisrTfs : 0u);
}

uint32_t FreescaleSsiStatus::Bits() {
    uint32_t bits = tx_.TxStatus() | rx_.RxStatus();
    if (!rules_.frame_complete) bits &= ~(ssi::kSisrTfrc | ssi::kSisrRfrc);
    return bits | Forced();
}

/* MCIMX51RM §56.4.3 / §56.4.4 (page 56-74): with RIE and RE, RFF0/1 "if the corresponding Receive
   FIFO is enabled", else RDR0/1; with TIE and TE, TFE0/1 "if corresponding Transmit FIFO is
   enabled", else TDE0/1. */
/* MCIMX51RM Table 56-14 (page 56-46): each enable bit "controls whether the corresponding status
   bit in SISR can issue an interrupt to the Core or not". */
uint32_t FreescaleSsiStatus::Sources() const {
    uint32_t src = 0u;
    /* MCIMX31RM §45.1.2.2.1 (page 45-10): "the interrupts related to this channel are generated only
       if this mode of operation is selected". */
    const uint32_t channels = ctl_.two_channel ? 2u : 1u;
    if ((ctl_.sier & ssi::kSierTie) != 0u && ctl_.te) {
        src |= ssi::kSisrTfs | ssi::kSisrTls;
        for (uint32_t k = 0; k < channels; ++k) {
            src |= ssi::Fifo(ssi::kSisrTue0, k);
            src |= ctl_.tfen[k] ? ssi::Fifo(ssi::kSisrTfe0, k) : ssi::Fifo(ssi::kSisrTde0, k);
        }
    }
    uint32_t enables = ctl_.sier;
    if ((ctl_.sier & ssi::kSierRie) != 0u && ctl_.re) {
        src |= ssi::kSisrRfs | ssi::kSisrRls;
        for (uint32_t k = 0; k < channels; ++k) {
            src |= ssi::Fifo(ssi::kSisrRoe0, k);
            src |= ctl_.rfen[k] ? ssi::Fifo(ssi::kSisrRff0, k) : ssi::Fifo(ssi::kSisrRdr0, k);
            /* MCIMX51RM Table 56-13 RDR0 (page 56-43): "If RIE and RDR0_EN are set ... if the FIFO
               is enabled, the interrupt is issued on RFF0 assertion." */
            if (ctl_.rfen[k] && (ctl_.sier & ssi::Fifo(ssi::kSisrRdr0, k)) != 0u) {
                enables |= ssi::Fifo(ssi::kSisrRff0, k);
            }
        }
    }
    if (rules_.frame_complete) src |= ssi::kSisrTfrc | ssi::kSisrRfrc;
    return src & enables;
}

void FreescaleSsiStatus::Drive(bool level) {
    if (level == asserted_) return;
    asserted_ = level;
    line_(level);
}

void FreescaleSsiStatus::Update() {
    const uint32_t sources = Sources();
    Drive((Bits() & sources) != 0u);
    const uint32_t watch = sources & ~Forced();
    uint64_t       at    = FreescaleSsiTransmitter::kNever;
    if (watch != 0u) at = std::min(tx_.NextStatusCycle(watch), rx_.NextStatusCycle(watch));
    if (at == FreescaleSsiTransmitter::kNever) {
        clock_->Disarm(ev_);
        return;
    }
    clock_->Arm(ev_, at);
}

/* MCIMX51RM Table 56-13 TLS / RLS (pages 56-44 / 56-45): "cleared when the SISR is read with this
   bit set"; MCIMX31RM Table 45-10 (pages 45-36 / 45-37): TUE0 "cleared by reading the SISR with
   TUE0 bit set, followed by writing to the STX0 register", ROE0 "followed by reading the SRX0". */
uint32_t FreescaleSsiStatus::ReadSisr() {
    const uint32_t bits = Bits();
    if ((bits & ssi::kSisrTls) != 0u) tx_.ClearLastSlot();
    if ((bits & ssi::kSisrRls) != 0u) rx_.ClearLastSlot();
    if (!rules_.write_one_clears) {
        for (uint32_t k = 0; k < 2u; ++k) {
            tue_read_[k] = (bits & ssi::Fifo(ssi::kSisrTue0, k)) != 0u;
            roe_read_[k] = (bits & ssi::Fifo(ssi::kSisrRoe0, k)) != 0u;
        }
    }
    Update();
    return bits;
}

/* MCIMX51RM Table 56-13 (page 56-44): TUE0 / ROE0 "also cleared by writing '1' to this bit". */
void FreescaleSsiStatus::WriteSisr(uint32_t value) {
    if (!rules_.write_one_clears) return;
    for (uint32_t k = 0; k < 2u; ++k) {
        if ((value & ssi::Fifo(ssi::kSisrTue0, k)) != 0u) tx_.ClearUnderrun(k);
        if ((value & ssi::Fifo(ssi::kSisrRoe0, k)) != 0u) rx_.ClearOverrun(k);
    }
    Update();
}

void FreescaleSsiStatus::StxWrite(uint32_t k) {
    if (tue_read_[k]) {
        tx_.ClearUnderrun(k);
        tue_read_[k] = false;
    }
    Update();
}

void FreescaleSsiStatus::SrxRead(uint32_t k) {
    if (roe_read_[k]) {
        rx_.ClearOverrun(k);
        roe_read_[k] = false;
    }
    Update();
}

void FreescaleSsiStatus::Save(StateWriter& w) {
    w.Write<uint8_t>("ssi_irq", asserted_ ? 1u : 0u);
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        w.Write<uint8_t>(("ssi_tue_read" + n).c_str(), tue_read_[k] ? 1u : 0u);
        w.Write<uint8_t>(("ssi_roe_read" + n).c_str(), roe_read_[k] ? 1u : 0u);
    }
}

void FreescaleSsiStatus::Restore(StateReader& r) {
    uint8_t irq = 0;
    r.Read("ssi_irq", irq);
    asserted_ = irq != 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        const std::string n = std::to_string(k);
        uint8_t tue = 0, roe = 0;
        r.Read(("ssi_tue_read" + n).c_str(), tue);
        r.Read(("ssi_roe_read" + n).c_str(), roe);
        tue_read_[k] = tue != 0u;
        roe_read_[k] = roe != 0u;
    }
    clock_->Disarm(ev_);
}

void FreescaleSsiStatus::PostRestore(const FreescaleSsiStatusControl& c) {
    ctl_ = c;
    line_(asserted_);
    Update();
}

}
