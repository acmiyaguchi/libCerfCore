#pragma once

#include "../oscillator_ticks.h"

#include <cstdint>
#include <functional>

namespace cerfengine { class MipsInterruptChannel; }

namespace cerfcore {

class CerfEmulator;

enum class Vr41xxRtcxDomain { RtcIcuPmu, Peripheral };

class Vr41xxRtcxTicks : public OscillatorTicks {
public:
    static constexpr uint32_t kHz = 32768u;

    Vr41xxRtcxTicks(CerfEmulator& emu, Vr41xxRtcxDomain domain)
        : OscillatorTicks(emu, domain == Vr41xxRtcxDomain::RtcIcuPmu), domain_(domain) {}

    void Attach();
    void Attach(std::function<void()> on_clock_gate);
    bool Running() { return !ClockStopped(); }

protected:
    uint64_t ClockCycles() override;
    bool     ClockStopped() override;

private:
    void Bind();

    const Vr41xxRtcxDomain domain_;
    cerfengine::MipsInterruptChannel*  channel_ = nullptr;
};

}
