#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class StateReader;
class StateWriter;

class Imx51CcmDvfs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void     Reset();
    void     SetRateListener(std::function<void()> fn) { rates_ = std::move(fn); }
    uint32_t Cdcr() const { return cdcr_; }
    void     WriteCdcr(uint32_t value);
    uint32_t Cdhipr() const;
    bool     WriteArmPodf(uint32_t podf);
    uint32_t ArmPodf() const { return arm_podf_; }
    uint64_t BusDivider() const { return bus_div_; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void UpdateBusDivider();

    std::function<void()> rates_;
    uint32_t cdcr_        = 0;
    uint32_t arm_podf_    = 0;
    bool     arm_held_    = false;
    bool     bus_divided_ = false;
    uint64_t bus_div_     = 1;
};

}
