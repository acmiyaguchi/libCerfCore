#include "sa11xx_external_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa1100_id.h"
#include "sa1110_id.h"
#include "sa11xx_gpio.h"
#include "sa11xx_gpio_clock_input.h"

REGISTER_SERVICE(Sa11xxExternalClocks);

bool Sa11xxExternalClocks::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxExternalClocks::OnReady() {
    gpio_  = &emu_.Get<Sa11xxGpio>();
    input_ = emu_.TryGet<Sa11xxGpioClockInput>();
    SampleChanged();
    if (input_ != nullptr) input_->RegisterChangeListener([this] { SampleChanged(); NotifyChange(); });
    gpio_->RegisterPinConfigListener([this] {
        if (SampleChanged()) NotifyChange();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Resync(); });
}

void Sa11xxExternalClocks::Resync() {
    SampleChanged();
}

/* SA-1110 Table 11-5 (printed 11-5): GPIO 18 and GPIO 20 "Sample clock input to UART"; GPIO 19
   "Clock input pin for SSP"; GPIO 21 "Clock input pin for MCP". */
bool Sa11xxExternalClocks::SampleChanged() {
    bool changed = false;
    for (uint32_t i = 0; i < kPinCount; ++i) {
        PinClock now{};
        now.running = Rate(kFirstPin + i, now.hz);
        if (now == clocks_[i]) continue;
        clocks_[i] = now;
        changed    = true;
    }
    return changed;
}

/* SA-1110 §11.11.3.5 (printed 11-113): "the user must configure the GPIO pin as an input by clearing
   the corresponding bit in the GPIO pin direction register (GPDR) and switch control of the GPIO pin
   to the UART by setting the corresponding bit in the GPIO alternate function register (GAFR)". */
bool Sa11xxExternalClocks::Rate(uint32_t pin, GuestCycleClock::Rate& hz) const {
    const Sa11xxGpio::PinConfig cfg = gpio_->Pin(pin);
    return cfg.alternate && !cfg.output && input_ != nullptr && input_->Frequency(pin, hz);
}

void Sa11xxExternalClocks::RegisterChangeListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void Sa11xxExternalClocks::NotifyChange() {
    for (auto& fn : listeners_) fn();
}
