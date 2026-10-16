#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;
class Wm9713MasterClocks;

class Wm9713Clocks : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void                  Reset() { pager_ = 0u; }
    void                  WritePager(uint16_t r46);
    GuestCycleClock::Rate BitClock(uint16_t r44, uint16_t r46, bool pll_on) const;
    uint32_t              HifiDivider(uint16_t r44) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    const Wm9713MasterClocks* inputs_ = nullptr;
    uint32_t                  pager_  = 0;
};

}
