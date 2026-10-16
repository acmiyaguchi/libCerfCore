#pragma once

#include "../execution/guest_cycle_clock.h"
#include "freescale_uart_receiver.h"
#include "oscillator_ticks.h"

#include <cstdint>

class CerfEmulator;
class StateReader;
class StateWriter;

class FreescaleUartEscape {
public:
    explicit FreescaleUartEscape(CerfEmulator& emu) : ref_(emu, false) {}

    void Attach();
    void SetReference(GuestCycleClock::Rate module, uint32_t divider, bool running);
    void Rescale() { ref_.Rescale(); }
    void Reset();
    void Handle(const FreescaleUartReceiver::Events& events, bool escen, uint32_t esc_char,
                uint64_t timeout_ref_clocks);
    void ClearFlags(uint32_t usr1);

    uint32_t Usr1() const;

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    void OnStored(uint64_t ref_now, uint16_t entry, bool escen, uint32_t esc_char, uint64_t timeout_ref_clocks);

    StoppableOscillatorTicks ref_;
    bool     escf_  = false;
    uint32_t count_ = 0;
    uint64_t last_  = 0;
};
