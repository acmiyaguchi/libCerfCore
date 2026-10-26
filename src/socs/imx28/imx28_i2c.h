#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_dma_device.h"
#include "imx28_apb_peripheral.h"
#include "imx28_i2c_master.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Imx28Icoll;

/* MCIMX28RM Rev. 2 chapter 27 (I2C), one instance per unit. */
class Imx28I2c : public Imx28ApbPeripheral, private Imx28ApbDmaDevice {
public:
    Imx28I2c(CerfEmulator& emu, uint32_t base, uint32_t error_irq, uint32_t dma_channel,
             bool board_slave)
        : Imx28ApbPeripheral(emu), master_(emu, base), base_(base), error_irq_(error_irq),
          dma_channel_(dma_channel), board_slave_(board_slave) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override { DriveLine(true); }

protected:
    uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes) override;

private:
    enum class Pio : uint8_t { Idle, Collect, Drain };

    void     ResetBlock();
    void     DriveLine(bool force);
    uint32_t ComposeStat() const;
    Imx28I2cMaster::Command Decode() const;
    void     CheckCtrl0(uint32_t value, uint32_t addr) const;
    void     WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteCtrl1(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteTiming(uint32_t& reg, Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteDebug0(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     StartPio(uint32_t addr);
    void     WriteData(uint32_t value, uint32_t addr);
    uint32_t ReadData(uint32_t addr);
    void     Transmit(const uint8_t* data);
    void     Complete(bool keep_rx);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    void     DmaPioWrite(uint32_t channel, uint32_t index, uint32_t value) override;
    Transfer DmaToMemory(uint32_t channel, uint8_t* dst, uint32_t count) override;
    Transfer DmaFromMemory(uint32_t channel, const uint8_t* src, uint32_t count) override;
    bool     DmaCommandEnded(uint32_t channel) override;
    void     DmaChannelReset(uint32_t channel) override;
    void     CheckDmaCommand(bool transmit, uint32_t count) const;

    Imx28Icoll*     icoll_ = nullptr;
    Imx28I2cMaster  master_;
    const uint32_t  base_;
    const uint32_t  error_irq_;
    const uint32_t  dma_channel_;
    const bool      board_slave_;

    uint32_t ctrl0_   = 0u;
    uint32_t timing0_ = 0u;
    uint32_t timing1_ = 0u;
    uint32_t timing2_ = 0u;
    uint32_t ctrl1_   = 0u;
    uint32_t debug0_  = 0u;
    bool     dma_armed_ = false;
    bool     line_      = false;
    Pio      pio_       = Pio::Idle;
    uint32_t pio_pos_   = 0u;
    bool     tx_held_   = false;
    bool     got_nak_   = false;
    uint32_t tx_word_   = 0u;
    std::vector<uint8_t> pio_buf_;
};

}
