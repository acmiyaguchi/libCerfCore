#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>

class IrqController;
class StateReader;
class StateWriter;

class Pxa255I2sInterrupt : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kTfs = 1u << 3, kRfs = 1u << 4, kTur = 1u << 5, kRor = 1u << 6;
    static constexpr uint64_t kNever = UINT64_MAX;

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(std::function<bool()> enabled, std::function<uint32_t(uint64_t)> live,
                std::function<uint64_t(uint64_t, uint32_t, uint32_t)> next);

    uint32_t Saimr() const { return saimr_; }
    uint32_t Sticky() const { return (tur_ ? kTur : 0u) | (ror_ ? kRor : 0u); }
    void     WriteSaimr(uint32_t value);
    void     WriteSaicr(uint64_t now, uint32_t value);
    void     NoteUnderrun() { tur_ = true; }
    void     NoteOverrun() { ror_ = true; }
    void     Refresh();
    void     ResetRegisters();

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

private:
    void Drive(bool level);

    GuestCycleClock*        clock_ = nullptr;
    IrqController*          intc_  = nullptr;
    GuestCycleClock::Event* ev_    = nullptr;
    std::function<bool()>                                 enabled_;
    std::function<uint32_t(uint64_t)>                     live_;
    std::function<uint64_t(uint64_t, uint32_t, uint32_t)> next_;

    uint32_t saimr_ = 0;
    bool     tur_   = false;
    bool     ror_   = false;
    bool     line_  = false;
};
