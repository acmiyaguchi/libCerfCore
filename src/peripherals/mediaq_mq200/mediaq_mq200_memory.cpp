#include "mediaq_mq200_memory.h"

#include "mediaq_mq200_power.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kMm00R = 0x4000u;
constexpr uint32_t kMm01R = 0x4004u;
constexpr uint32_t kMm02R = 0x4008u;
constexpr uint32_t kMm04R = 0x4010u;
constexpr uint32_t kMmBlockLo = 0x4000u;
constexpr uint32_t kMmBlockHi = 0x6000u;

/* MQ-200 Data Book Table 5-89 .. Table 5-93. */
constexpr uint32_t kMm00MiuEnable   = 1u << 0;
constexpr uint32_t kMm00MiuResetOff = 1u << 1;
constexpr uint32_t kMm00Reserved    = 0xFFFFFFF8u;
constexpr uint32_t kMm01BusClock    = 1u << 0;
constexpr uint32_t kMm01Reserved    = 0x3C000F00u;
constexpr uint32_t kMm04Reserved    = 0xFFF0E0E0u;

bool Mm02ThresholdsLegal(uint32_t v) {
    const uint32_t fifo4[] = {(v >> 10) & 0xFu, (v >> 14) & 0xFu, (v >> 18) & 0xFu,
                              (v >> 22) & 0xFu};
    for (uint32_t t : fifo4) {
        if (t == 0u) return false;
    }
    return ((v >> 26) & 7u) != 0u && ((v >> 29) & 7u) != 0u;
}

}

void MediaQMq200Memory::PowerOnReset() {
    mm00_ = mm01_ = mm02_ = mm03_ = mm04_ = 0u;
}

bool MediaQMq200Memory::Owns(uint32_t roff) {
    return roff >= kMmBlockLo && roff < kMmBlockHi;
}

uint32_t MediaQMq200Memory::Read(uint32_t roff) const {
    switch (roff) {
        case kMm00R: return mm00_;
        case kMm01R: return mm01_;
        case kMm02R: return mm02_;
        case kMm03R: return mm03_;
        case kMm04R: return mm04_;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Memory: read of undecoded register 0x%05X", roff);
}

void MediaQMq200Memory::Write(uint32_t roff, uint32_t value) {
    switch (roff) {
        case kMm00R:
            if (value & kMm00Reserved) {
                emu_.Get<Fatal>().Die("MediaQMq200Memory: MM00R 0x%08X sets reserved bits", value);
            }
            if ((value & kMm00MiuEnable) && !(value & kMm00MiuResetOff)) {
                emu_.Get<Fatal>().Die("MediaQMq200Memory: MM00R 0x%08X enables the MIU with its "
                                      "reset enabled; not modeled", value);
            }
            mm00_ = value;
            return;
        case kMm01R:
            if (value & kMm01Reserved) {
                emu_.Get<Fatal>().Die("MediaQMq200Memory: MM01R 0x%08X sets reserved bits", value);
            }
            mm01_ = value;
            return;
        case kMm02R:
            if (!Mm02ThresholdsLegal(value)) {
                emu_.Get<Fatal>().Die("MediaQMq200Memory: MM02R 0x%08X sets an illegal zero FIFO "
                                      "threshold", value);
            }
            mm02_ = value;
            return;
        case kMm03R:
            mm03_ = value;
            return;
        case kMm04R:
            if (value & kMm04Reserved) {
                emu_.Get<Fatal>().Die("MediaQMq200Memory: MM04R 0x%08X sets reserved bits", value);
            }
            mm04_ = value;
            return;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Memory: write 0x%08X to undecoded register 0x%05X", value,
                          roff);
}

const char* MediaQMq200Memory::MiuPowerDown() const {
    if (!(mm00_ & kMm00MiuEnable)) return "MM00R powers the MIU down";
    if (!power_.Powered(MediaQMq200Power::kModMiu)) return "the MIU is powered down in this D-state";
    return nullptr;
}

bool MediaQMq200Memory::MiuPoweredDown() const {
    return MiuPowerDown() != nullptr;
}

const char* MediaQMq200Memory::MiuBlocked() const {
    if (power_.InD3()) return "the chip is in D3";
    if (const char* why = MiuPowerDown()) return why;
    const auto clk = power_.State((mm01_ & kMm01BusClock) ? MediaQMq200Power::kBusClock
                                                          : MediaQMq200Power::kPll1);
    if (clk.kind == MediaQMq200Power::ClockState::Stopped) return "the memory clock is stopped";
    if (clk.kind == MediaQMq200Power::ClockState::Unmodeled) return clk.why;
    return nullptr;
}

void MediaQMq200Memory::SaveState(StateWriter& w) const {
    w.Write("mm00", mm00_);
    w.Write("mm01", mm01_);
    w.Write("mm02", mm02_);
    w.Write("mm03", mm03_);
    w.Write("mm04", mm04_);
}

void MediaQMq200Memory::RestoreState(StateReader& r) {
    r.Read("mm00", mm00_);
    r.Read("mm01", mm01_);
    r.Read("mm02", mm02_);
    r.Read("mm03", mm03_);
    r.Read("mm04", mm04_);
}

}
