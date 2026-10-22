#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_pinctrl_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Imx28Icoll;

/* MCIMX28RM Rev. 2 chapter 9 (PINCTRL) at 0x8001_8000. */
class Imx28Pinctrl : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80018000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    using PinLevelFn = std::function<bool(uint32_t bank, uint32_t pin, bool* high)>;
    void SetPinLevelProvider(PinLevelFn fn);

    using DinConsumedFn = std::function<uint32_t(uint32_t bank)>;
    void SetDinConsumedProvider(DinConsumedFn fn);

    void NotifyPinLevelChanged(uint32_t bank, uint32_t pin);

    uint32_t MuxSelect(uint32_t bank, uint32_t pin) const;
    bool     SamplePin(uint32_t bank, uint32_t pin, const char* who) const;

protected:
    uint32_t ReadLanes(uint32_t word_addr, uint32_t lanes) override;
    uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes) override;

private:
    static constexpr uint32_t kBanks = 5u;

    uint32_t DecodeRow(const char* op, uint32_t addr, uint32_t value, Imx28SctOp* sct_op) const;
    uint32_t Reg(uint32_t offset) const;
    uint32_t& RegRef(uint32_t offset);
    void     LoadResetValues(bool power_on);
    uint32_t BankLevels(uint32_t bank, uint32_t* known) const;
    void     UpdateBanks();
    void     DriveLines(bool force);
    uint32_t ComposeRead(uint32_t row, uint32_t lanes);

    uint32_t regs_[kImx28PinctrlRegCount] = {};
    uint32_t level_[kBanks] = {};
    uint32_t armed_[kBanks] = {};
    uint32_t line_ = 0u;
    Imx28Icoll* icoll_ = nullptr;
    PinLevelFn pin_level_;
    DinConsumedFn din_consumed_;
};

}
