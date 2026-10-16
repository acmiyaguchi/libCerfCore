#pragma once

#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

class CerfEmulator;
class StateWriter;
class StateReader;

class MediaQMq200Power {
public:
    enum Clock : uint32_t { kBusClock = 0, kPll1 = 1, kPll2 = 2, kPll3 = 3 };

    struct ClockState {
        enum Kind : uint8_t { Stopped, Running, Unmodeled };
        Kind                  kind = Stopped;
        GuestCycleClock::Rate rate;
        const char*           why  = nullptr;
    };

    static constexpr uint32_t kModOsc     = 1u << 0;
    static constexpr uint32_t kModPll1    = 1u << 1;
    static constexpr uint32_t kModPll2    = 1u << 2;
    static constexpr uint32_t kModPll3    = 1u << 3;
    static constexpr uint32_t kModMiu     = 1u << 4;
    static constexpr uint32_t kModGe      = 1u << 6;
    static constexpr uint32_t kModFp      = 1u << 9;
    static constexpr uint32_t kModGc1     = 1u << 16;
    static constexpr uint32_t kModWin1    = 1u << 17;
    static constexpr uint32_t kModAltWin1 = 1u << 18;
    static constexpr uint32_t kModCursor1 = 1u << 19;
    static constexpr uint32_t kModGc2     = 1u << 24;
    static constexpr uint32_t kModWin2    = 1u << 25;
    static constexpr uint32_t kModAltWin2 = 1u << 26;
    static constexpr uint32_t kModCursor2 = 1u << 27;

    explicit MediaQMq200Power(CerfEmulator& emu) : emu_(emu) {}

    void Configure(uint32_t osc_hz, uint32_t bus_mode);
    void PowerOnReset();

    static bool Owns(uint32_t roff);
    static bool D3Readable(uint32_t roff);
    static bool D3Writable(uint32_t roff);

    uint32_t Read(uint32_t roff) const;
    void     Write(uint32_t roff, uint32_t value);
    void     LatchD3BusWrite(uint32_t value) { state_ = value & 3u; }

    bool       InD3() const { return state_ == kD3; }
    bool       Powered(uint32_t module) const;
    bool       StateEnables(uint32_t module) const;
    ClockState State(uint32_t clock) const;
    bool        GePowered() const;
    const char* GeBlocked() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kD3 = 3u;

    ClockState  OscState() const;
    ClockState  PllState(uint32_t pll, uint32_t prog, bool enabled, uint32_t module) const;
    const char* GePowerDown() const;

    CerfEmulator&           emu_;
    uint32_t                osc_hz_   = 0;
    uint32_t                bus_mode_ = 0;

    uint32_t state_ = kD3;
    uint32_t dc00_  = 0;
    uint32_t dc02_  = 0;
    uint32_t dc03_  = 0;
    uint32_t pm00_  = 0;
    uint32_t pm01_  = 0;
    uint32_t pm02_  = 0;
    uint32_t pm06_  = 0;
    uint32_t pm07_  = 0;
};
