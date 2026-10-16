#include "mediaq_mq200_interrupts.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kInBlockLo = 0x8000u;
constexpr uint32_t kInBlockHi = 0xA000u;

/* MQ-200 Data Book Table 5-103 .. Table 5-105. */
constexpr uint32_t kIn00PinEnable = 1u << 0;
constexpr uint32_t kIn00Reserved  = 0xFFFFFFE0u;
constexpr uint32_t kInReserved    = 0xFFFF0000u;

}

void MediaQMq200Interrupts::PowerOnReset() {
    control_ = mask_ = status_ = 0u;
}

bool MediaQMq200Interrupts::Owns(uint32_t roff) {
    return roff >= kInBlockLo && roff < kInBlockHi;
}

uint32_t MediaQMq200Interrupts::Read(uint32_t roff) const {
    switch (roff) {
        case kIn00R: return control_;
        case kIn01R: return mask_;
        case kIn02R: return status_;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Interrupts: read of 0x%05X (IN03R raw status or undecoded) "
                          "is not modeled", roff);
}

void MediaQMq200Interrupts::Write(uint32_t roff, uint32_t value) {
    switch (roff) {
        case kIn00R:
            if (value & (kIn00Reserved | kIn00PinEnable)) {
                emu_.Get<Fatal>().Die("MediaQMq200Interrupts: IN00R 0x%08X sets reserved bits or "
                                      "the interrupt pin enable", value);
            }
            control_ = value;
            return;
        case kIn01R:
            if (value & ~kGc1VdeFalling) {
                emu_.Get<Fatal>().Die("MediaQMq200Interrupts: IN01R 0x%08X unmasks a source other "
                                      "than the GC1 VDE falling edge; not modeled", value);
            }
            mask_ = value;
            return;
        case kIn02R:
            if (value & kInReserved) {
                emu_.Get<Fatal>().Die("MediaQMq200Interrupts: IN02R 0x%08X sets reserved bits",
                                      value);
            }
            status_ &= ~value;
            return;
    }
    emu_.Get<Fatal>().Die("MediaQMq200Interrupts: write 0x%08X to 0x%05X (IN03R raw status or "
                          "undecoded) is not modeled", value, roff);
}

void MediaQMq200Interrupts::OnGc1VdeFalling() {
    if (mask_ & kGc1VdeFalling) status_ |= kGc1VdeFalling;
}

void MediaQMq200Interrupts::SaveState(StateWriter& w) const {
    w.Write("in00", control_);
    w.Write("in01", mask_);
    w.Write("in02", status_);
}

void MediaQMq200Interrupts::RestoreState(StateReader& r) {
    r.Read("in00", control_);
    r.Read("in01", mask_);
    r.Read("in02", status_);
}

}
