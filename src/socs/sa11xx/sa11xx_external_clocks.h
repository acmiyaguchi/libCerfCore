#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class Sa11xxGpio;
class Sa11xxGpioClockInput;

class Sa11xxExternalClocks : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Rate(uint32_t pin, GuestCycleClock::Rate& hz) const;
    void RegisterChangeListener(std::function<void()> fn);
    void Resync();

private:
    static constexpr uint32_t kFirstPin = 18u;
    static constexpr uint32_t kPinCount = 4u;

    struct PinClock {
        bool                  running = false;
        GuestCycleClock::Rate hz{};

        bool operator==(const PinClock&) const = default;
    };

    bool SampleChanged();
    void NotifyChange();

    Sa11xxGpio*           gpio_  = nullptr;
    Sa11xxGpioClockInput* input_ = nullptr;
    std::array<PinClock, kPinCount>    clocks_{};
    std::vector<std::function<void()>> listeners_;
};

}
