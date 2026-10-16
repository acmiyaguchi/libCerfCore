#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>

class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255MdhIndexCount = 2u;

class Msm8255ClockRates : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t GrantMdhRateKhz(uint32_t index, uint32_t min_khz,
                             uint32_t max_khz);
    uint32_t GrantClockFreqHz(uint32_t clock, uint32_t freq_hz,
                              uint32_t match);
    uint32_t ReportClockFreqKhz(uint32_t clock);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    uint32_t SelectRateHz(const uint32_t* rates, uint32_t count,
                          uint32_t clock, uint32_t freq_hz, uint32_t match);

    std::atomic<uint32_t> mdh_granted_khz_[kMsm8255MdhIndexCount] = {};
    std::atomic<uint32_t> mdp_core_granted_hz_{0u};
};
