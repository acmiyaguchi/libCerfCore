#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/state/state_stream.h>
#include "imx28_pinctrl_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 9 (PINCTRL) at 0x8001_8000. */
class Imx28Pinctrl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80018000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* Board hook: level of the pad outside the chip (a peripheral driving a muxed pin, or the
       external circuit on a GPIO input). Returns false when the board model has no level. */
    using PinLevelFn = std::function<bool(uint32_t bank, uint32_t pin, bool* high)>;
    void SetPinLevelProvider(PinLevelFn fn);

    /* Board hook: the level of bank/pin changed outside the chip; latches IRQSTAT edges now. */
    void NotifyPinLevelChanged(uint32_t bank, uint32_t pin);

    uint32_t MuxSelect(uint32_t bank, uint32_t pin) const;
    bool     SamplePin(uint32_t bank, uint32_t pin, const char* who) const;

private:
    static constexpr uint32_t kBanks = 5u;

    uint32_t DecodeRow(const char* op, uint32_t addr, uint32_t value, Imx28SctOp* sct_op) const;
    uint32_t Reg(uint32_t offset) const;
    uint32_t& RegRef(uint32_t offset);
    void     LoadResetValues(bool power_on);
    void     RequireOperating(const char* op, uint32_t addr, uint32_t value) const;
    uint32_t BankLevels(uint32_t bank, uint32_t* known) const;
    void     UpdateBanks();
    uint32_t ComposeRead(uint32_t row, uint32_t lanes);
    uint32_t ReadReg(uint32_t addr, uint32_t lanes);

    uint32_t regs_[kImx28PinctrlRegCount] = {};
    uint32_t level_[kBanks] = {};
    uint32_t armed_[kBanks] = {};
    PinLevelFn pin_level_;
};

}
