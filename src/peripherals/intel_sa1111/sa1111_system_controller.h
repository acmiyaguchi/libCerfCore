#pragma once

#include "sa1111_sbi.h"
#include "sa1111_unit.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>
#include <vector>

class Sa11xxTestUnit;

class Sa1111SystemController : public Sa1111Unit {
public:
    using Sa1111Unit::Sa1111Unit;

    bool ShouldRegister() override;

    uint32_t MmioBase() const override { return 0x40000200u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void RegisterClockListener(std::function<void()> fn);
    void NotifyClockListeners();
    void OnSleepEntry();
    void OnDozeEntry();

    bool AudioFrameRate(GuestCycleClock::Rate& rate) const;
    bool SspClockRunning() const;
    bool DmaClockRunning() const;
    Sa1111Sbi::BusGrant DmaGrant() const;
    bool RclkRunning() const;
    bool L3ClockRunning() const;

protected:
    void     OnUnitReady() override;
    void     OnChipReset(bool held) override;
    uint32_t UnitReadWord (uint32_t addr) override;
    void     UnitWriteWord(uint32_t addr, uint32_t value) override;

private:
    void LoadResetValues();
    bool PllOutput(GuestCycleClock::Rate& rate) const;
    bool Enabled(uint32_t skpcr_bit) const { return (regs_[0] & skpcr_bit) != 0u; }

    Sa1111Sbi*      sbi_       = nullptr;
    Sa11xxTestUnit* test_unit_ = nullptr;
    std::vector<std::function<void()>> clock_listeners_;
    uint32_t regs_[9] = {};
};
