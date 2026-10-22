#pragma once

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "imx28_apb_peripheral.h"
#include "imx28_clkctrl_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 10 (CLKCTRL) at 0x8004_0000. */
class Imx28Clkctrl : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80040000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    GuestCycleClock::Rate XbusRate() const;
    bool                  WatchdogPorDisabled() const;
    void                  RegisterApbxListener(std::function<void()> fn);

private:
    uint32_t                 ComposeRead(uint32_t index) const;
    bool                     PllLocked(uint32_t pll) const;
    GuestCycleClock::Rate    DividerRate(GuestCycleClock::Rate ref, uint32_t div, bool frac_en,
                                         uint32_t div_bits, const char* what) const;
    GuestCycleClock::Rate    CpuRate() const;
    void                     ApplyRates();
    void                     LoadResetValues();
    void                     CheckDividerWrite(uint32_t index, uint32_t old, uint32_t next);
    void                     WriteRegister(uint32_t index, Imx28SctOp op, uint32_t value);
    void                     ToggleStableBits(uint32_t index, uint32_t old_value);
    uint32_t                 DecodeIndex(const char* op, uint32_t addr, uint32_t value,
                                         Imx28SctOp* sct_op) const;

    uint32_t regs_[kImx28ClkctrlRegCount] = {};
    uint64_t applied_hz_ = 0u;
    std::vector<std::function<void()>> apbx_listeners_;
};

}
