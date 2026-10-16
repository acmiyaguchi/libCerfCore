#include "pxa255_i2s_fifo_reset_sync.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "pxa255_id.h"

namespace {

/* Intel PXA255 Developer's Manual Table 14-6 (page 14-11) SACR1: DREC 3, DRPL 4; section 14.6.2 (page 14-10):
   "It takes 4 BITCLK cycles and 4 internal clock cycles before these controls are conveyed to the slower
   BITCLK domain". */
constexpr uint32_t kSacr1Drec = 1u << 3, kSacr1Drpl = 1u << 4;
constexpr uint64_t kSyncBits  = 4u;

}  // namespace

REGISTER_SERVICE(Pxa255I2sFifoResetSync);

bool Pxa255I2sFifoResetSync::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

/* Intel PXA255 Developer's Manual section 14.6.2 (page 14-10): if DRPL / DREC "are modified at a rate faster
   than (4 BITCLK + 4 internal clock) cycles, the last updated value in this time frame is stored in a temporary
   register and is transferred to the BITCLK domain". */
void Pxa255I2sFifoResetSync::Apply(uint32_t sacr1, uint32_t old, uint64_t bits, uint64_t first) {
    ApplyOne(sacr1 & kSacr1Drpl, old & kSacr1Drpl, bits, first, tx_);
    ApplyOne(sacr1 & kSacr1Drec, old & kSacr1Drec, bits, first, rx_);
}

/* Intel PXA255 Developer's Manual section 14.3.2 (page 14-4): "Asserting the DRPL bit in SACR1 has the following
   effects", among them "Transmit FIFO pointers are reset to zero". */
void Pxa255I2sFifoResetSync::ApplyOne(uint32_t now_set, uint32_t was_set, uint64_t bits, uint64_t first,
                                      Pending& p) {
    if (now_set != 0u && was_set == 0u && !p.pending) {
        p.pending    = true;
        p.frame      = first;
        p.assert_bit = bits;
    } else if (now_set == 0u && was_set != 0u && p.pending && bits < p.assert_bit + kSyncBits) {
        p.pending = false;
    }
}

void Pxa255I2sFifoResetSync::Cancel() {
    tx_ = Pending{};
    rx_ = Pending{};
}

bool Pxa255I2sFifoResetSync::TxPending(uint64_t& frame) const {
    frame = tx_.frame;
    return tx_.pending;
}

bool Pxa255I2sFifoResetSync::RxPending(uint64_t& frame) const {
    frame = rx_.frame;
    return rx_.pending;
}

void Pxa255I2sFifoResetSync::Save(StateWriter& w) {
    w.Write<uint8_t>("i2s_tx_reset_pending", tx_.pending ? 1u : 0u);
    w.Write<uint64_t>("i2s_tx_reset_frame", tx_.frame);
    w.Write<uint64_t>("i2s_tx_assert_bit", tx_.assert_bit);
    w.Write<uint8_t>("i2s_rx_reset_pending", rx_.pending ? 1u : 0u);
    w.Write<uint64_t>("i2s_rx_reset_frame", rx_.frame);
    w.Write<uint64_t>("i2s_rx_assert_bit", rx_.assert_bit);
}

void Pxa255I2sFifoResetSync::Restore(StateReader& r) {
    uint8_t tx_pending = 0, rx_pending = 0;
    r.Read("i2s_tx_reset_pending", tx_pending);
    r.Read("i2s_tx_reset_frame", tx_.frame);
    r.Read("i2s_tx_assert_bit", tx_.assert_bit);
    r.Read("i2s_rx_reset_pending", rx_pending);
    r.Read("i2s_rx_reset_frame", rx_.frame);
    r.Read("i2s_rx_assert_bit", rx_.assert_bit);
    tx_.pending = tx_pending != 0u;
    rx_.pending = rx_pending != 0u;
}
