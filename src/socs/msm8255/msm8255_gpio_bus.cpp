#include "msm8255_gpio_bus.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

bool Msm8255GpioBus::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255GpioBus::RegisterWindow(Msm8255GpioWindow* window) {
    std::lock_guard<std::mutex> lk(mtx_);
    windows_.push_back(window);
    RedriveOwnedPinsLocked(window);
}

void Msm8255GpioBus::SetInputPin(uint32_t pin, bool high) {
    if (pin >= kPinCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 gpio bus: gpio %u is past the %u pins the controller "
            "addresses", pin, kPinCount);
    }

    driven_[pin].store(high ? kDrivenHigh : kDrivenLow,
                       std::memory_order_release);

    std::lock_guard<std::mutex> lk(mtx_);
    for (Msm8255GpioWindow* window : windows_) {
        if (!window->OwnsGpioPin(pin)) continue;
        window->SetGpioInputPin(pin, high);
        return;
    }
}

void Msm8255GpioBus::RedriveOwnedPins(Msm8255GpioWindow* window) {
    std::lock_guard<std::mutex> lk(mtx_);
    RedriveOwnedPinsLocked(window);
}

void Msm8255GpioBus::RedriveOwnedPinsLocked(Msm8255GpioWindow* window) {
    for (uint32_t pin = 0; pin < kPinCount; ++pin) {
        const uint8_t level = driven_[pin].load(std::memory_order_acquire);
        if (level == kUndriven) continue;
        if (!window->OwnsGpioPin(pin)) continue;
        window->SetGpioInputPin(pin, level == kDrivenHigh);
    }
}

REGISTER_SERVICE(Msm8255GpioBus);
