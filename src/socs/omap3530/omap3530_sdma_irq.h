#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

class StateReader;
class StateWriter;

class Omap3530SdmaIrq : public Peripheral {
public:
    using Peripheral::Peripheral;

protected:
    virtual int      IrqForLine(int j) const = 0;
    virtual uint32_t FiredChannels() const   = 0;

    static bool IsIrqRegister(uint32_t off) { return off >= kIrqStatusL0 && off < kIrqEnableEnd; }

    uint32_t ReadIrqRegister(uint32_t off) const;
    void     WriteIrqRegister(uint32_t off, uint32_t value);
    void     ResetIrqRegisters();
    void     UpdateIrqLines();

    void SaveIrqRegisters(StateWriter& w);
    void RestoreIrqRegisters(StateReader& r);
    void SaveIrqLines(StateWriter& w);
    void RestoreIrqLines(StateReader& r);

private:
    static constexpr uint32_t kIrqStatusL0  = 0x008u;
    static constexpr uint32_t kIrqEnableL0  = 0x018u;
    static constexpr uint32_t kIrqEnableEnd = 0x028u;

    uint32_t irqstatus_l_[4]   = {0, 0, 0, 0};
    uint32_t irqenable_l_[4]   = {0, 0, 0, 0};
    bool     irq_line_high_[4] = {false, false, false, false};
};
