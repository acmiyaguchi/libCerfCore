#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class Imx51Gpio1;
class StateReader;
class StateWriter;

class Imx51CcmStatus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void     Reset(uint32_t ccr, uint32_t clpcr);
    void     OnControl(uint32_t ccr, uint32_t clpcr);
    void     Raise(uint32_t cisr_bits);
    uint32_t Csr(uint32_t clpcr) const;
    uint32_t Cisr();
    uint32_t Cimr();
    uint32_t Cwdr();
    void     WriteCisr(uint32_t value);
    void     WriteCimr(uint32_t value);
    void     WriteCwdr(uint32_t value);
    bool     FpmOn() const { return source_on_[kFpm]; }
    bool     Camp1On() const { return source_on_[kCamp1]; }
    bool     Camp2On() const { return source_on_[kCamp2]; }

    GuestCycleClock::Rate OscillatorRate() const {
        return GuestCycleClock::Rate{source_on_[kCosc] ? osc_hz_ : 0u, 1u};
    }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    enum SourceId : uint32_t { kCosc, kCamp2, kCamp1, kFpm, kSourceCount };

    void OnGpioInput(uint32_t pin, bool level);
    void RaiseLevelsLocked();
    void RaiseLocked(uint32_t bits);
    void DriveLocked();

    Imx51Gpio1*                      gpio1_ = nullptr;
    uint64_t                         osc_hz_ = 0;
    std::array<bool, kSourceCount>   source_on_{};
    std::mutex                       mu_;
    uint32_t                         cisr_   = 0;
    uint32_t                         cimr_   = 0;
    uint32_t                         cwdr_   = 0;
    uint32_t                         levels_ = 0;
};

}
