#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

class Imx28Icoll;

/* MCIMX28RM Rev. 2 chapter 32 (USBPHY), one instance per unit. */
class Imx28Usbphy : public Imx28ApbPeripheral {
public:
    Imx28Usbphy(CerfEmulator& emu, uint32_t unit, uint32_t base)
        : Imx28ApbPeripheral(emu), unit_(unit), base_(base) {}

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
    static constexpr uint32_t kSlots = 6u;

    void     LoadResetValues();
    uint32_t ComposeCtrl() const;
    void     WriteSlot(uint32_t slot, Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     DriveLine(bool force);

    uint32_t    unit_;
    uint32_t    base_;
    Imx28Icoll* icoll_ = nullptr;
    uint32_t    regs_[kSlots] = {};
    bool        line_ = false;
};

}
