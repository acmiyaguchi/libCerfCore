#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/peripherals/peripheral_base.h>

#include <array>
#include <cstdint>

class Imx51Dpll : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    GuestCycleClock::Rate OutputRate() const { return output_; }
    GuestCycleClock::Rate ReferenceRate() const;
    bool                  Restarted() const { return restarted_; }
    void                  OnInputChange();

protected:
    virtual uint32_t              LockInterruptBit() const = 0;
    virtual GuestCycleClock::Rate BootOutputRate() const = 0;
    GuestCycleClock::Rate         BootReferenceRate() const;

private:
    using Rate = GuestCycleClock::Rate;

    static constexpr uint32_t kSize = 0x00001000u;

    enum class Lock : uint8_t { kOff, kRelocking, kLocked, kBroken };

    struct Port {
        uint32_t ctl = 0;
        uint32_t op  = 0;
        uint32_t mfd = 0;
        uint32_t mfn = 0;
    };

    bool     Running() const;
    void     ResetRegisters();
    void     Restart();
    void     StopOutput();
    void     OnLock();
    void     OnLoadAck();
    void     SetOutput(Rate rate);
    void     ScheduleLoad();
    Rate     ComputeRate() const;
    Rate     SelectedInputRate() const;
    void     FollowReference();

    std::array<uint32_t, kSize / 4> regs_{};
    Port port_;
    Rate output_{0u, 1u};
    Rate target_{0u, 1u};
    Rate input_{0u, 1u};
    Lock lock_        = Lock::kOff;
    bool restarted_   = false;
    bool load_wanted_ = false;
};
