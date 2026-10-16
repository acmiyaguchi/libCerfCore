#pragma once

#include <cerfcore/core/service.h>
#include "msm8255_gpio_banks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class Msm8255GpioWindow {
public:
    virtual ~Msm8255GpioWindow() = default;

    virtual bool OwnsGpioPin(uint32_t pin) const  = 0;
    virtual void SetGpioInputPin(uint32_t pin, bool high) = 0;
};

class Msm8255GpioBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void RegisterWindow(Msm8255GpioWindow* window);

    void SetInputPin(uint32_t pin, bool high);

    void RedriveOwnedPins(Msm8255GpioWindow* window);

private:
    static constexpr uint32_t kPinCount = kMsm8255GpioPinCount;

    enum : uint8_t { kUndriven = 0u, kDrivenLow = 1u, kDrivenHigh = 2u };

    void RedriveOwnedPinsLocked(Msm8255GpioWindow* window);

    mutable std::mutex                            mtx_;
    std::vector<Msm8255GpioWindow*>               windows_;
    std::array<std::atomic<uint8_t>, kPinCount>   driven_{};
};
