#include "mediaq_mq200_ge_window.h"

#include "mediaq_mq200_ge.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint32_t kGeBlockLo     = 0xC000u;
constexpr uint32_t kGe0BR         = 0xC02Cu;
constexpr uint32_t kGe40R         = 0xC100u;
constexpr uint32_t kGe5FR         = 0xC17Cu;
constexpr uint32_t kGe80R         = 0xC200u;
constexpr uint32_t kGeSecondaryHi = 0xC230u;
constexpr uint32_t kGeBlockHi     = 0xE000u;
constexpr uint32_t kSrcFifo       = 0x18000u;

constexpr uint32_t kGe00 = 0u;
constexpr uint32_t kGe0A = 10u;
constexpr uint32_t kGe0B = 11u;

/* MQ-200 Data Book Tables 5-63 .. 5-74 (idx 153-167) and Table 5-82: bits of GE00R-GE0BR /
   GE80R-GE8BR that are Reserved, undefined or test mode under every definition. */
constexpr uint32_t kReservedAlways[MediaQMq200GeWindow::kCommandRegs] = {
    0xF0000000u, 0x80000000u, 0x00000000u, 0xE0000000u, 0x00000000u, 0xF000F000u,
    0xF000F000u, 0x00000000u, 0x00000000u, 0x81FF0000u, 0x3FFFF000u, 0xFFE00000u};

constexpr uint32_t kGe0ADepthReserved   = 2u;
constexpr uint32_t kGe0BUnimplemented   = 0x7u;

bool PrimaryRegister(uint32_t roff) {
    return roff <= kGe0BR || (roff >= kGe40R && roff <= kGe5FR);
}

}

void MediaQMq200GeWindow::PowerOnReset() {
    std::memset(secondary_, 0, sizeof(secondary_));
}

bool MediaQMq200GeWindow::Owns(uint32_t roff) {
    return (roff >= kGeBlockLo && roff < kGeBlockHi) || roff == kSrcFifo;
}

uint32_t MediaQMq200GeWindow::Read(uint32_t roff, const char* blocked) {
    if (blocked) {
        emu_.Get<Fatal>().Die("MediaQMq200: GE read of 0x%05X while %s", roff, blocked);
    }
    if (PrimaryRegister(roff)) return ge_.ReadReg((roff - kGeBlockLo) / 4u);
    if (roff > kGe80R && roff < kGeSecondaryHi) return secondary_[(roff - kGe80R) / 4u];
    emu_.Get<Fatal>().Die("MediaQMq200: GE read of reserved or unmodeled register 0x%05X", roff);
}

void MediaQMq200GeWindow::Write(uint32_t roff, uint32_t value, const char* blocked) {
    if (blocked) {
        emu_.Get<Fatal>().Die("MediaQMq200: GE write 0x%08X to 0x%05X while %s", value, roff,
                              blocked);
    }
    if (roff == kSrcFifo) {
        ge_.PushSourceFifo(value);
        return;
    }
    const bool primary   = PrimaryRegister(roff);
    const bool secondary = roff > kGe80R && roff < kGeSecondaryHi;
    if (!primary && !secondary) {
        emu_.Get<Fatal>().Die("MediaQMq200: GE write 0x%08X to 0x%05X (the secondary command "
                              "pipeline or a reserved register) is not modeled", value, roff);
    }
    const uint32_t g = (roff - (primary ? kGeBlockLo : kGe80R)) / 4u;
    if (g < kCommandRegs) {
        if ((value & kReservedAlways[g]) || (g == kGe0A && (value >> 30) == kGe0ADepthReserved)) {
            emu_.Get<Fatal>().Die("MediaQMq200: GE write 0x%08X to 0x%05X sets reserved, "
                                  "undefined or test-mode bits", value, roff);
        }
        if (g == kGe0B) value &= ~kGe0BUnimplemented;
    }
    if (secondary) {
        secondary_[g] = value;
        return;
    }
    if (g == kGe00) {
        if (const char* why = ge_.CommandUnmodeled(value)) {
            emu_.Get<Fatal>().Die("MediaQMq200: GE00R command 0x%08X %s", value, why);
        }
    }
    ge_.WriteReg(g, value);
}

void MediaQMq200GeWindow::SaveState(StateWriter& w) const {
    w.WriteBytes("ge_secondary", secondary_, sizeof(secondary_));
}

void MediaQMq200GeWindow::RestoreState(StateReader& r) {
    r.ReadBytes("ge_secondary", secondary_, sizeof(secondary_));
}

}
