#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <optional>

namespace cerfcore {

class Omap3530GpioBankBase;

class Omap3530GpioBus : public Service {
public:
    using Service::Service;
    ~Omap3530GpioBus() override = default;

    bool ShouldRegister() override;

    /* Each Omap3530GpioBankBase concrete registers itself in its
       OnReady (bank_index 0..5 = GPIO1..GPIO6). */
    void RegisterBank(uint32_t bank_index, Omap3530GpioBankBase* bank);

    /* Absolute GPIO pin index 0..191 (TRM-numbered, same as the
       BSP's PenGPIO=0xAF). high=true drives the pin high; false
       drives it low. Routes pin/32 to the right bank and forwards
       to the bank's SetInputPin. Unregistered banks no-op. */
    void SetInputPin(uint32_t absolute_pin, bool high);

    std::optional<bool> DrivenLevel(uint32_t absolute_pin);

private:
    static constexpr uint32_t kBankCount = 6;
    std::array<Omap3530GpioBankBase*, kBankCount> banks_{};
};

}
