#include "mediaq_mq200_fp.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint32_t kFpBlockLo = 0xE000u;
constexpr uint32_t kFpBlockHi = 0x10000u;

constexpr uint32_t kFp00R = 0u;
constexpr uint32_t kFp01R = 1u;
constexpr uint32_t kFp02R = 2u;
constexpr uint32_t kFp03R = 3u;
constexpr uint32_t kFp04R = 4u;
constexpr uint32_t kFp05R = 5u;
constexpr uint32_t kFp0FR = 15u;
constexpr uint32_t kFp10R = 16u;

/* MQ-200 Data Book Table 5-51 .. Table 5-59. */
constexpr uint32_t kFp00Unmodeled  = 0xF0000C10u;
constexpr uint32_t kFp01Unmodeled  = 0xF8000801u;
constexpr uint32_t kFp02Unmodeled  = 0xFFE002AAu;
constexpr uint32_t kFp03Unmodeled  = 0xF8F8FFFFu;
constexpr uint32_t kFp03ReadOnly   = 0x07000000u;
constexpr uint32_t kFp0FMustBeZero = 0x00080008u;

bool Fp00Unmodeled(uint32_t v) {
    const uint32_t panel_type = (v >> 2) & 3u;
    const uint32_t width      = (v >> 5) & 7u;
    const uint32_t dither     = (v >> 12) & 7u;
    return (v & kFp00Unmodeled) || panel_type >= 2u || width > (panel_type == 0u ? 2u : 4u) ||
           dither == 1u || dither == 5u || dither == 7u;
}

}

void MediaQMq200Fp::PowerOnReset() {
    std::memset(regs_, 0, sizeof(regs_));
}

bool MediaQMq200Fp::Owns(uint32_t roff) {
    return roff >= kFpBlockLo && roff < kFpBlockHi;
}

uint32_t MediaQMq200Fp::Read(uint32_t roff) const {
    const uint32_t i = (roff - kFpBlockLo) / 4u;
    if (i < kRegs && i != kFp03R && (i <= kFp05R || i >= kFp0FR)) return regs_[i];
    emu_.Get<Fatal>().Die("MediaQMq200Fp: read of 0x%05X (FP03R pin inputs, reserved or "
                          "undecoded) is not modeled", roff);
}

void MediaQMq200Fp::Write(uint32_t roff, uint32_t value) {
    const uint32_t i = (roff - kFpBlockLo) / 4u;
    bool unmodeled = false;
    switch (i) {
        case kFp00R: unmodeled = Fp00Unmodeled(value); break;
        case kFp01R: unmodeled = (value & kFp01Unmodeled) != 0u; break;
        case kFp02R: unmodeled = (value & kFp02Unmodeled) != 0u; break;
        case kFp03R:
            unmodeled = (value & kFp03Unmodeled) != 0u;
            value &= ~kFp03ReadOnly;
            break;
        case kFp04R:
        case kFp05R:
            break;
        case kFp0FR:
            unmodeled = (value & kFp0FMustBeZero) || (value & 3u) == 3u ||
                        ((value >> 16) & 3u) == 3u;
            break;
        default:
            if (i < kFp10R || i >= kRegs) {
                emu_.Get<Fatal>().Die("MediaQMq200Fp: write 0x%08X to reserved or undecoded "
                                      "register 0x%05X", value, roff);
            }
            break;
    }
    if (unmodeled) {
        emu_.Get<Fatal>().Die("MediaQMq200Fp: FP%02XR 0x%08X sets a reserved encoding or an "
                              "unmodeled pin mode, panel type or test mode", i, value);
    }
    regs_[i] = value;
}

bool MediaQMq200Fp::PanelPinsHigh() const {
    constexpr uint32_t kPinGpo = 1u;
    constexpr struct {
        uint32_t mode_shift;
        uint32_t data_bit;
    } kPanelPins[] = {{0u, 16u}, {2u, 17u}, {8u, 20u}};
    const uint32_t v = regs_[kFp02R];
    for (const auto& pin : kPanelPins) {
        if (((v >> pin.mode_shift) & 3u) == kPinGpo && !((v >> pin.data_bit) & 1u)) return false;
    }
    return true;
}

uint32_t MediaQMq200Fp::PanelGc() const {
    switch (regs_[kFp00R] & 3u) {
        case 1u: return 0u;
        case 3u: return 1u;
        default: return kNoPanel;
    }
}

void MediaQMq200Fp::SaveState(StateWriter& w) const {
    w.WriteBytes("fp_regs", regs_, sizeof(regs_));
}

void MediaQMq200Fp::RestoreState(StateReader& r) {
    r.ReadBytes("fp_regs", regs_, sizeof(regs_));
}

}
