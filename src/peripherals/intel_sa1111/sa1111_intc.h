#pragma once

#include "sa1111_unit.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

class Sa1111Sbi;

class Sa1111Intc : public Sa1111Unit {
public:
    using Sa1111Unit::Sa1111Unit;

    bool ShouldRegister() override;

    uint32_t MmioBase() const override { return 0x40001600u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void     RegisterSampler(std::function<void()> sample);
    void     SetSourceLevel(uint8_t source, bool level);
    void     StrobeSource(uint8_t source);
    uint32_t ClockedSources1();
    uint32_t ClockPolarity1();

protected:
    void     OnUnitReady() override;
    void     OnChipReset(bool held) override;
    uint32_t UnitReadWord (uint32_t addr) override;
    void     UnitWriteWord(uint32_t addr, uint32_t value) override;

private:
    struct Bank {
        uint32_t raw      = 0;
        uint32_t test     = 0;
        uint32_t enable   = 0;
        uint32_t polarity = 0;
        uint32_t status   = 0;
        uint32_t wake_en  = 0;
        uint32_t wake_pol = 0;
        uint32_t clock    = 0;
    };

    bool     WakeMode() const;
    uint32_t LatchClock(const Bank& b) const;
    void     Clock(Bank& b);
    void     ClockBoth();
    bool     WriteClockInput(uint32_t off, uint32_t value);
    void     OnModeInputChange();
    void     DriveCascadeOutput(bool pulse_low_first);
    void     SampleSources();

    std::mutex mtx_;
    std::vector<std::function<void()>> samplers_;
    Sa1111Sbi* sbi_ = nullptr;

    Bank     bank_[2];
    uint32_t tstsel_ = 0;
    bool     sleep_  = false;
};

}
