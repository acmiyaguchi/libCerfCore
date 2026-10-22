#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <utility>

namespace cerfcore {

/* MCIMX28RM Rev. 2 Figure 38-2, Table 38-2: the 4-wire panel on LRADC2-LRADC5. */
class SharpBrainPwSh4TouchPanel : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void SetContact(bool down, uint32_t raw_x, uint32_t raw_y);
    bool OwnsPin(uint32_t pin) const;
    uint32_t PinMillivolts(uint32_t pin, uint32_t ctrl0);
    bool Detected(uint32_t ctrl0);

    void SetChangeHandler(std::function<void()> fn) { on_change_ = std::move(fn); }

private:
    std::function<void()> on_change_;
    bool     down_  = false;
    uint32_t raw_x_ = 0u;
    uint32_t raw_y_ = 0u;
};

}
