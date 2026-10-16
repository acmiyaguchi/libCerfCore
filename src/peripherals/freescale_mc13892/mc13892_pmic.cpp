#include "mc13892_pmic.h"

REGISTER_SERVICE(Mc13892Pmic);

void Mc13892Pmic::TxnStart(uint8_t /*slave_addr*/) {
    /* sub_addr_ persists across a repeated-START (write reg index, then read);
       only the next-write-is-an-index flag and the byte cursors reset. */
    sub_addr_pending_ = true;
    wr_idx_           = 0;
    rd_idx_           = 0;
}

void Mc13892Pmic::TxnWriteByte(uint8_t /*slave_addr*/, uint8_t byte) {
    if (sub_addr_pending_) {
        sub_addr_         = byte & (kRegCount - 1);
        sub_addr_pending_ = false;
        return;
    }
    if (wr_idx_ > 2) return;
    const uint32_t shift = 16u - 8u * wr_idx_;   /* MSB first */
    uint32_t& r = regs_[sub_addr_];
    r = (r & ~(0xFFu << shift)) | (static_cast<uint32_t>(byte) << shift);
    ++wr_idx_;
}

uint8_t Mc13892Pmic::TxnReadByte(uint8_t /*slave_addr*/) {
    const uint8_t idx   = rd_idx_ > 2 ? 2 : rd_idx_;
    const uint32_t shift = 16u - 8u * idx;       /* MSB first */
    if (rd_idx_ <= 2) ++rd_idx_;
    return static_cast<uint8_t>(regs_[sub_addr_] >> shift);
}
