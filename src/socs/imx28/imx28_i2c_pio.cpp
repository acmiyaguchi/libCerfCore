#include "imx28_i2c.h"

#include <cerfcore/core/cerf_emulator.h>
#include "imx28_i2c_regs.h"

#include <algorithm>

namespace cerfcore {

using namespace imx28_i2c;

/* § 27.3.1 steps 1-3; a read that fits the four byte receive FIFO ends the command (Linux
   v6.1 drivers/i2c/busses/i2c-mxs.c mxs_i2c_pio_setup_xfer waits for RUN clear, then reads). */
void Imx28I2c::StartPio(uint32_t addr) {
    RequireOperating("RUN", ctrl0_, addr, 0u);
    if ((ctrl0_ & kCtrl0Master) == 0u) Die("RUN in slave mode", addr, 0u);
    const Imx28I2cMaster::Command cmd = Decode();
    pio_buf_.assign(cmd.count, 0u);
    pio_pos_ = 0u;
    if ((ctrl0_ & kCtrl0Transmit) != 0u) {
        pio_ = Pio::Collect;
        if (tx_held_) {
            tx_held_ = false;
            WriteData(tx_word_, addr);
        }
    } else {
        master_.Receive(cmd, pio_buf_.data());
        pio_ = Pio::Drain;
        if (cmd.count <= kWordBytes) Complete(true);
    }
    if (pio_ != Pio::Idle) debug0_ |= kDebug0DmaReq;
}

/* § 27.3.1 step 4; Figure 27-9 (word 0x03020156 sends 0x56 first); Linux v6.1
   i2c-mxs.c mxs_i2c_pio_trigger_write_cmd writes DATA before setting RUN. */
void Imx28I2c::WriteData(uint32_t value, uint32_t addr) {
    if (pio_ == Pio::Idle && !tx_held_ && (ctrl0_ & kCtrl0Run) == 0u &&
        (ctrl0_ & (kCtrl0Master | kCtrl0Transmit)) == (kCtrl0Master | kCtrl0Transmit)) {
        tx_held_ = true;
        tx_word_ = value;
        return;
    }
    if (pio_ != Pio::Collect) Die("DATA write with no transmit pending", addr, value);
    const uint32_t left = static_cast<uint32_t>(pio_buf_.size()) - pio_pos_;
    const uint32_t n    = std::min(kWordBytes, left);
    for (uint32_t k = 0; k < n; ++k) {
        pio_buf_[pio_pos_ + k] = static_cast<uint8_t>(value >> (8u * k));
    }
    pio_pos_ += n;
    if (pio_pos_ < pio_buf_.size()) return;
    Transmit(pio_buf_.data());
}

/* Table 27-1 No Slave Ack sets NO_SLAVE_ACK_IRQ; § 27.5.6 GOT_A_NAK; Linux v6.1 i2c-mxs.c
   mxs_i2c_xfer_msg: after a NAK "the controller halts until it gets told to return to idle". */
void Imx28I2c::Transmit(const uint8_t* data) {
    if (master_.Transmit(Decode(), data)) {
        Complete(false);
        return;
    }
    if (dma_armed_) Die("no slave acknowledges address byte in a DMA command", base_, data[0]);
    ctrl1_  |= kCtrl1NoSlaveAck;
    got_nak_ = true;
    pio_     = Pio::Idle;
    pio_pos_ = 0u;
    pio_buf_.clear();
    DriveLine(false);
}

/* § 27.3.1 steps 17-20 (word layout not defined); Figure 27-9 byte order; Linux v6.1
   i2c-mxs.c mxs_i2c_pio_setup_xfer takes the first byte from bits 7:0. */
uint32_t Imx28I2c::ReadData(uint32_t addr) {
    if (pio_ != Pio::Drain) Die("DATA read with no receive pending", addr, 0u);
    const uint32_t left = static_cast<uint32_t>(pio_buf_.size()) - pio_pos_;
    const uint32_t n    = std::min(kWordBytes, left);
    uint32_t word = 0u;
    for (uint32_t k = 0; k < n; ++k) word |= static_cast<uint32_t>(pio_buf_[pio_pos_ + k]) << (8u * k);
    pio_pos_ += n;
    if (pio_pos_ >= pio_buf_.size()) {
        if ((ctrl0_ & kCtrl0Run) != 0u) Complete(false);
        pio_     = Pio::Idle;
        pio_pos_ = 0u;
        pio_buf_.clear();
    }
    return word;
}

/* § 27.5.1 PRE_SEND_START, POST_SEND_STOP cleared by hardware, RUN reset at the end of the
   command, CLOCK_HELD set after RETAIN_CLOCK; § 27.5.5 DATA_ENGINE_CMPLT_IRQ; Table 27-1
   BUS_FREE_IRQ on stop. */
void Imx28I2c::Complete(bool keep_rx) {
    const bool retain = (ctrl0_ & kCtrl0Retain) != 0u;
    const bool stop   = (ctrl0_ & kCtrl0PostStop) != 0u;
    ctrl0_ &= ~(kCtrl0Run | kCtrl0PreStart | kCtrl0PostStop | kCtrl0ClockHeld | kCtrl0CountMask);
    if (retain) ctrl0_ |= kCtrl0ClockHeld;
    ctrl1_ |= kCtrl1DataEngineCmplt;
    if (stop) ctrl1_ |= kCtrl1BusFreeIrq;
    dma_armed_ = false;
    if (!keep_rx) {
        pio_     = Pio::Idle;
        pio_pos_ = 0u;
        pio_buf_.clear();
    }
    DriveLine(false);
}

}
