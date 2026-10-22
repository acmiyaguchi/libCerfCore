#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 35 (SAIF), one instance per unit. */
class Imx28Saif : public Imx28ApbPeripheral {
public:
    Imx28Saif(CerfEmulator& emu, uint32_t unit, uint32_t base)
        : Imx28ApbPeripheral(emu), unit_(unit), base_(base) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;

private:
    void WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    uint32_t unit_;
    uint32_t base_;
    uint32_t ctrl_ = 0u;
};

}
