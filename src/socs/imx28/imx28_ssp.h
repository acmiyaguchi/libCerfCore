#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_dma_device.h"
#include "imx28_apb_peripheral.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

class Imx28Icoll;
class MmcCard;

/* MCIMX28RM Rev. 2 chapter 17 (SSP) in SD/MMC mode, one instance per unit. */
class Imx28Ssp : public Imx28ApbPeripheral, private Imx28ApbDmaDevice {
public:
    Imx28Ssp(CerfEmulator& emu, uint32_t unit, uint32_t base, uint32_t error_irq)
        : Imx28ApbPeripheral(emu), unit_(unit), base_(base), error_irq_(error_irq) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override { DriveLine(true); }

private:
    void     ResetBlock();
    void     DriveLine(bool force);
    void     WriteRegister(uint32_t reg, Imx28SctOp op, uint32_t value, uint32_t addr);
    uint32_t CheckReserved(uint32_t next, uint32_t writable, uint32_t addr, uint32_t value) const;
    void     WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteTiming(uint32_t value, uint32_t addr);
    MmcCard* Card();
    void     CheckRun(uint32_t addr) const;
    bool     IssueCommand(uint32_t addr);
    void     RunCommandOnly(uint32_t addr);
    uint32_t BeginData(bool to_memory, uint32_t count);
    void     EndData();
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    void     DmaPioWrite(uint32_t channel, uint32_t index, uint32_t value) override;
    Transfer DmaToMemory(uint32_t channel, uint8_t* dst, uint32_t count) override;
    Transfer DmaFromMemory(uint32_t channel, const uint8_t* src, uint32_t count) override;
    bool     DmaCommandEnded(uint32_t channel) override;
    void     DmaChannelReset(uint32_t) override {}

    uint32_t    unit_;
    uint32_t    base_;
    uint32_t    error_irq_;
    Imx28Icoll* icoll_ = nullptr;
    bool        line_  = false;

    uint32_t ctrl0_      = 0u;
    uint32_t cmd0_       = 0u;
    uint32_t cmd1_       = 0u;
    uint32_t xfer_size_  = 0u;
    uint32_t block_size_ = 0u;
    uint32_t compref_    = 0u;
    uint32_t compmask_   = 0u;
    uint32_t timing_     = 0u;
    uint32_t ctrl1_      = 0u;
    uint32_t ddr_ctrl_   = 0u;
    uint32_t dll_ctrl_   = 0u;
    uint32_t status_     = 0u;
    uint32_t sdresp_[4]  = {};
    bool     dma_pio_    = false;
};

}
