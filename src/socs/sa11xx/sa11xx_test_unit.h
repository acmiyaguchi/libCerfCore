#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class Sa11xxTestUnit : public Service {
public:
    using Service::Service;

    enum class Mbgnt { Arbiter, Low, High, Undetermined };

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Read() const { return tucr_; }
    void     Write(uint32_t value);

    Mbgnt MbgntPin() const;
    bool  Gp27ClockRate(GuestCycleClock::Rate& rate) const;

    void RegisterChangeListener(std::function<void()> fn);

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    void NotifyChange();

    std::vector<std::function<void()>> listeners_;
    uint32_t tucr_ = 0;
};

}
