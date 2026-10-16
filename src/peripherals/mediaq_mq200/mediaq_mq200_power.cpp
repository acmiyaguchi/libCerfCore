#include "mediaq_mq200_power.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace {

constexpr uint32_t kPm00R = 0x0000u;
constexpr uint32_t kPm01R = 0x0004u;
constexpr uint32_t kPm02R = 0x0008u;
constexpr uint32_t kPm06R = 0x0018u;
constexpr uint32_t kPm07R = 0x001Cu;
constexpr uint32_t kDc00R = 0x14000u;
constexpr uint32_t kDc01R = 0x14004u;
constexpr uint32_t kDc02R = 0x14008u;
constexpr uint32_t kDc03R = 0x1400Cu;
constexpr uint32_t kPc00R = 0x16000u;
constexpr uint32_t kPc08R = 0x16008u;
constexpr uint32_t kPmr   = 0x16040u;
constexpr uint32_t kPmcsr = 0x16044u;

constexpr uint32_t kPmBlockEnd = 0x2000u;
constexpr uint32_t kDcBlockLo  = 0x14000u;
constexpr uint32_t kPciBlockLo = 0x16000u;
constexpr uint32_t kPciBlockHi = 0x18000u;

/* MQ-200 Data Book Table 5-98 (PM00R). */
constexpr uint32_t kPm00MustBeZero = 0xF8C000D3u;
constexpr uint32_t kPm00ReadOnly   = 0x07000000u;
constexpr uint32_t kPm00Unmodeled  = 0x0000E600u;
constexpr uint32_t kPm00Pll2Enable = 1u << 2;
constexpr uint32_t kPm00Pll3Enable = 1u << 3;
constexpr uint32_t kPm00GeEnable   = 1u << 8;
constexpr uint32_t kPm00GeClkShift = 11u;

/* MQ-200 Data Book Table 5-99 / Table 5-100 (PM01R / PM02R). */
constexpr uint32_t kPmStateMustBeZero = 0xF0F0FC80u;

/* MQ-200 Data Book Table 5-101 / Table 5-102 (PM06R / PM07R). */
constexpr uint32_t kPllProgMustBeZero = 0x0F00E08Cu;
constexpr uint32_t kPllExternalRef    = (1u << 0) | (1u << 1);
constexpr uint32_t kPllMaxP           = 4u;

constexpr uint32_t kDcOscBypass   = 1u << 0;
constexpr uint32_t kDcOscEnable   = 1u << 1;
constexpr uint32_t kDcPll1Bypass  = 1u << 2;
constexpr uint32_t kDcPll1Enable  = 1u << 3;
constexpr uint32_t kDcChipReset   = 1u << 14;

constexpr uint32_t kDeviceId = 0x02004D51u;

/* MQ-200 Data Book Table 5-87 (PMR). */
constexpr uint32_t kPmrValue = 0x06210001u;

/* MQ-200 Data Book Table 5-94: fIN >= 1 MHz, 170 MHz <= fVCO <= 440 MHz, M 40 to 256. */
constexpr uint64_t kPllMinInHz  = 1000000u;
constexpr uint64_t kPllMinVcoHz = 170000000u;
constexpr uint64_t kPllMaxVcoHz = 440000000u;
constexpr uint64_t kPllMinM     = 40u;

uint32_t PllP(uint32_t prog) { return (prog >> 4) & 7u; }

/* MQ-200 Data Book Table 5-99 / Table 5-100 bits 4, 16, 24. */
uint32_t PowerDependencies(uint32_t module) {
    switch (module) {
        case MediaQMq200Power::kModGe:
        case MediaQMq200Power::kModGc1:
        case MediaQMq200Power::kModGc2:
            return MediaQMq200Power::kModMiu;
        case MediaQMq200Power::kModWin1:
        case MediaQMq200Power::kModCursor1:
            return MediaQMq200Power::kModGc1 | MediaQMq200Power::kModMiu;
        case MediaQMq200Power::kModAltWin1:
            return MediaQMq200Power::kModWin1 | MediaQMq200Power::kModGc1 |
                   MediaQMq200Power::kModMiu;
        case MediaQMq200Power::kModWin2:
        case MediaQMq200Power::kModCursor2:
            return MediaQMq200Power::kModGc2 | MediaQMq200Power::kModMiu;
        case MediaQMq200Power::kModAltWin2:
            return MediaQMq200Power::kModWin2 | MediaQMq200Power::kModGc2 |
                   MediaQMq200Power::kModMiu;
        default:
            return 0u;
    }
}

}

void MediaQMq200Power::Configure(uint32_t osc_hz, uint32_t bus_mode) {
    osc_hz_   = osc_hz;
    bus_mode_ = bus_mode;
}

void MediaQMq200Power::PowerOnReset() {
    state_ = kD3;
    dc00_ = dc02_ = dc03_ = 0u;
    pm00_ = pm01_ = pm02_ = pm06_ = pm07_ = 0u;
}

bool MediaQMq200Power::Owns(uint32_t roff) {
    return roff < kPmBlockEnd || (roff >= kDcBlockLo && roff < kPciBlockHi);
}

bool MediaQMq200Power::D3Readable(uint32_t roff) {
    return roff == kDc00R || roff == kDc01R || (roff >= kPciBlockLo && roff < kPciBlockHi);
}

bool MediaQMq200Power::D3Writable(uint32_t roff) {
    return roff == kDc00R || roff == kPmcsr;
}

uint32_t MediaQMq200Power::Read(uint32_t roff) const {
    switch (roff) {
        case kPm00R: return (pm00_ & ~kPm00ReadOnly) | (state_ << 24);
        case kPm01R: return pm01_;
        case kPm02R: return pm02_;
        case kPm06R: return pm06_;
        case kPm07R: return pm07_;
        case kDc00R: return dc00_;
        case kDc01R: return bus_mode_;
        case kDc02R: return dc02_;
        case kDc03R: return dc03_;
        case kPc00R: return kDeviceId;
        case kPc08R: return 0u;
        case kPmr:   return kPmrValue;
        case kPmcsr: return state_;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Power: read of undecoded register 0x%05X", roff);
}

void MediaQMq200Power::Write(uint32_t roff, uint32_t value) {
    switch (roff) {
        case kPm00R:
            if (value & kPm00MustBeZero) {
                emu_.Get<Fatal>().Die("MediaQMq200Power: PM00R 0x%08X sets must-be-zero bits",
                                      value);
            }
            if (value & kPm00Unmodeled) {
                emu_.Get<Fatal>().Die("MediaQMq200Power: PM00R 0x%08X sets a GE force-busy, GE "
                                      "FIFO reset or MIU power-sequencing bit; not modeled", value);
            }
            pm00_ = value & ~kPm00ReadOnly;
            return;
        case kPm01R:
        case kPm02R:
            if (value & kPmStateMustBeZero) {
                emu_.Get<Fatal>().Die("MediaQMq200Power: PM%02XR 0x%08X sets must-be-zero bits",
                                      roff / 4u, value);
            }
            (roff == kPm01R ? pm01_ : pm02_) = value;
            return;
        case kPm06R:
        case kPm07R:
            if ((value & kPllProgMustBeZero) || PllP(value) > kPllMaxP) {
                emu_.Get<Fatal>().Die("MediaQMq200Power: PM%02XR 0x%08X sets must-be-zero bits or "
                                      "a reserved P", roff / 4u, value);
            }
            (roff == kPm06R ? pm06_ : pm07_) = value;
            return;
        case kDc00R:
            if ((value & kDcChipReset) || PllP(value) > kPllMaxP) {
                emu_.Get<Fatal>().Die("MediaQMq200Power: DC00R 0x%08X sets the software chip reset "
                                      "or a reserved PLL1 P; not modeled", value);
            }
            dc00_ = value;
            return;
        case kDc02R: dc02_ = value; return;
        case kDc03R: dc03_ = value; return;
        case kPmcsr: state_ = value & 3u; return;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Power: write 0x%08X to read-only or undecoded register "
                          "0x%05X", value, roff);
}

bool MediaQMq200Power::Powered(uint32_t module) const {
    uint32_t enables = 0u;
    switch (state_) {
        case 0u: return true;
        case 1u: enables = pm01_; break;
        case 2u: enables = pm02_; break;
        default: return false;
    }
    const uint32_t needed = module | PowerDependencies(module);
    return (enables & needed) == needed;
}

bool MediaQMq200Power::StateEnables(uint32_t module) const {
    switch (state_) {
        case 1u: return (pm01_ & module) != 0u;
        case 2u: return (pm02_ & module) != 0u;
        default: return false;
    }
}

MediaQMq200Power::ClockState MediaQMq200Power::OscState() const {
    ClockState s;
    if (!(dc00_ & kDcOscEnable) || !Powered(kModOsc)) return s;
    if (dc00_ & kDcOscBypass) {
        s.kind = ClockState::Unmodeled;
        s.why  = "DC00R bypasses the internal oscillator";
        return s;
    }
    s.kind = ClockState::Running;
    s.rate = GuestCycleClock::Rate{osc_hz_, 1u};
    return s;
}

MediaQMq200Power::ClockState MediaQMq200Power::PllState(uint32_t pll, uint32_t prog, bool enabled,
                                                        uint32_t module) const {
    const ClockState osc = OscState();
    if (!enabled || !Powered(module) || osc.kind == ClockState::Stopped) return ClockState{};
    if (osc.kind == ClockState::Unmodeled) return osc;
    ClockState     s;
    const uint64_t f = osc.rate.num;
    const uint64_t m = ((prog >> 16) & 0xFFu) + 1u;
    const uint64_t n = ((prog >> 8) & 0x1Fu) + 1u;
    if (m < kPllMinM || f < kPllMinInHz * n || f * m < kPllMinVcoHz * n ||
        f * m > kPllMaxVcoHz * n) {
        s.kind = ClockState::Unmodeled;
        s.why  = pll == kPll1 ? "PLL1 is programmed outside its M / fIN / fVCO range"
                              : "PLL2 or PLL3 is programmed outside its M / fIN / fVCO range";
        return s;
    }
    s.kind = ClockState::Running;
    s.rate = GuestCycleClock::Rate{f * m, n << PllP(prog)}.Reduced();
    return s;
}

MediaQMq200Power::ClockState MediaQMq200Power::State(uint32_t clock) const {
    switch (clock) {
        case kBusClock: {
            ClockState s;
            s.kind = ClockState::Unmodeled;
            s.why  = "the bus interface clock has no modeled rate";
            return s;
        }
        case kPll1:
            if (dc00_ & kDcPll1Bypass) return OscState();
            return PllState(kPll1, dc00_, (dc00_ & kDcPll1Enable) != 0u, kModPll1);
        case kPll2:
        case kPll3: {
            const uint32_t prog = clock == kPll2 ? pm06_ : pm07_;
            const bool     on   = (pm00_ & (clock == kPll2 ? kPm00Pll2Enable : kPm00Pll3Enable)) != 0u;
            if (prog & kPllExternalRef) {
                ClockState s;
                s.kind = ClockState::Unmodeled;
                s.why  = "PLL2 or PLL3 takes the L2CLK / L3CLK input";
                return s;
            }
            return PllState(clock, prog, on, clock == kPll2 ? kModPll2 : kModPll3);
        }
    }
    emu_.Get<Fatal>().Die("MediaQMq200Power: clock source %u", clock);
}

const char* MediaQMq200Power::GePowerDown() const {
    if (state_ == kD3) return "the chip is in D3";
    if (!(pm00_ & kPm00GeEnable)) return "PM00R leaves the GE disabled";
    if (!Powered(kModGe)) return "the GE is powered down in this D-state";
    return nullptr;
}

bool MediaQMq200Power::GePowered() const {
    return GePowerDown() == nullptr;
}

const char* MediaQMq200Power::GeBlocked() const {
    if (const char* why = GePowerDown()) return why;
    const ClockState clk = State((pm00_ >> kPm00GeClkShift) & 3u);
    if (clk.kind == ClockState::Stopped) return "the GE clock is stopped";
    if (clk.kind == ClockState::Unmodeled) return clk.why;
    return nullptr;
}

void MediaQMq200Power::SaveState(StateWriter& w) const {
    w.Write("pmcsr", state_);
    w.Write("dc00", dc00_);
    w.Write("dc02", dc02_);
    w.Write("dc03", dc03_);
    w.Write("pm00", pm00_);
    w.Write("pm01", pm01_);
    w.Write("pm02", pm02_);
    w.Write("pm06", pm06_);
    w.Write("pm07", pm07_);
}

void MediaQMq200Power::RestoreState(StateReader& r) {
    r.Read("pmcsr", state_);
    r.Read("dc00", dc00_);
    r.Read("dc02", dc02_);
    r.Read("dc03", dc03_);
    r.Read("pm00", pm00_);
    r.Read("pm01", pm01_);
    r.Read("pm02", pm02_);
    r.Read("pm06", pm06_);
    r.Read("pm07", pm07_);
}
