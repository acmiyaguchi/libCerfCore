#include "pr31x00_lcd_gray_scale.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x10C00038u;

enum RegIndex : uint32_t {
    kCtl5  = 0,
    kCtl6  = 1,
    kCtl7  = 2,
    kCtl8  = 3,
    kCtl9  = 4,
    kCtl10 = 5,
    kCtl11 = 6,
    kCtl12 = 7,
    kCtl13 = 8,
    kCtl14 = 9,
};

constexpr const char* kRegNames[] = {"ctl5",  "ctl6",  "ctl7",  "ctl8",  "ctl9",
                                     "ctl10", "ctl11", "ctl12", "ctl13", "ctl14"};

constexpr uint32_t kPat2_3Recommended       = 0x000007DAu;   /* §17.4.8  CTL8  */
constexpr uint32_t kPat3_4Pat2_4Recommended = 0x7DBEA5A5u;   /* §17.4.9  CTL9  */
constexpr uint32_t kPat4_5Recommended       = 0x0007DFBEu;   /* §17.4.10 CTL10 */
constexpr uint32_t kPat3_5Recommended       = 0x0007A5ADu;   /* §17.4.11 CTL11 */
constexpr uint32_t kPat6_7Recommended       = 0x0FBFDFE7u;   /* §17.4.12 CTL12 */
constexpr uint32_t kPat5_7Recommended       = 0x07B5ADEFu;   /* §17.4.13 CTL13 */
constexpr uint32_t kPat4_7Recommended       = 0x0B9DC663u;   /* §17.4.14 CTL14 */

/* Reserved bits: §17.4.7 CTL7 <31:16>; §17.4.8 CTL8 <31:12>; §17.4.10 CTL10 and §17.4.11 CTL11
   <31:20>; §17.4.12 CTL12, §17.4.13 CTL13 and §17.4.14 CTL14 <31:28>. */
constexpr uint32_t kCtl7Reserved  = 0xFFFF0000u;
constexpr uint32_t kCtl8Reserved  = 0xFFFFF000u;
constexpr uint32_t kPat20Reserved = 0xFFF00000u;
constexpr uint32_t kPat28Reserved = 0xF0000000u;

}

bool Pr31x00LcdGrayScale::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00LcdGrayScale::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* §17.4.5-§17.4.14: Video Control 5-14 are write-only. */
uint32_t Pr31x00LcdGrayScale::ReadWord(uint32_t addr) {
    HaltUnsupportedAccess("PR31x00 LCD ReadWord", addr, 0);
}

void Pr31x00LcdGrayScale::WriteWord(uint32_t addr, uint32_t value) {
    switch ((addr - kBase) / 4u) {
        case kCtl5: reg_[kCtl5] = value; return;
        case kCtl6: reg_[kCtl6] = value; return;

        case kCtl7:
            if (value & kCtl7Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL7 reserved", addr, value);
            }
            reg_[kCtl7] = value;
            return;

        case kCtl8:
            if (value & kCtl8Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL8 reserved", addr, value);
            }
            StorePattern(kCtl8, addr, value, kPat2_3Recommended, "PR31x00 LCD PAT2_3 dither pattern");
            return;

        case kCtl9:
            StorePattern(kCtl9, addr, value, kPat3_4Pat2_4Recommended, "PR31x00 LCD PAT3_4/PAT2_4 dither pattern");
            return;

        case kCtl10:
            if (value & kPat20Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL10 reserved", addr, value);
            }
            StorePattern(kCtl10, addr, value, kPat4_5Recommended, "PR31x00 LCD PAT4_5 dither pattern");
            return;

        case kCtl11:
            if (value & kPat20Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL11 reserved", addr, value);
            }
            StorePattern(kCtl11, addr, value, kPat3_5Recommended, "PR31x00 LCD PAT3_5 dither pattern");
            return;

        case kCtl12:
            if (value & kPat28Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL12 reserved", addr, value);
            }
            StorePattern(kCtl12, addr, value, kPat6_7Recommended, "PR31x00 LCD PAT6_7 dither pattern");
            return;

        case kCtl13:
            if (value & kPat28Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL13 reserved", addr, value);
            }
            StorePattern(kCtl13, addr, value, kPat5_7Recommended, "PR31x00 LCD PAT5_7 dither pattern");
            return;

        case kCtl14:
            if (value & kPat28Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL14 reserved", addr, value);
            }
            StorePattern(kCtl14, addr, value, kPat4_7Recommended, "PR31x00 LCD PAT4_7 dither pattern");
            return;

        default:
            HaltUnsupportedAccess("PR31x00 LCD WriteWord", addr, value);
    }
}

void Pr31x00LcdGrayScale::StorePattern(uint32_t idx, uint32_t addr, uint32_t value,
                                       uint32_t recommended, const char* op) {
    if (value != recommended) {
        HaltUnsupportedAccess(op, addr, value);
    }
    reg_[idx] = value;
}

/* §17.3.7 (printed p.17-14): gray scale is "the duty cycle (i.e., number of frames) for which a
   given pixel is turned on", 2-bit gray selects a BLUESEL nibble (§17.4.7, printed p.17-24);
   §17.4.1 (printed p.17-20) INVVID: "the VDAT[3:0] signals to be inverted". */
uint32_t Pr31x00LcdGrayScale::ShadeFor(uint32_t raw, uint32_t bits_per_pixel, bool inverted) const {
    uint32_t vdat;
    if (bits_per_pixel == 2u) {
        vdat = (reg_[kCtl7] >> (4u * raw)) & 0xFu;
    } else if (bits_per_pixel == 1u) {
        vdat = raw ? 0xFu : 0u;
    } else {
        vdat = raw;
    }
    const uint32_t on_duty = inverted ? (0xFu - vdat) : vdat;
    return 0xFu - on_duty;
}

void Pr31x00LcdGrayScale::SaveState(StateWriter& w) {
    static_assert(sizeof(kRegNames) / sizeof(kRegNames[0]) == kRegs);
    for (uint32_t i = 0; i < kRegs; ++i) w.Write(kRegNames[i], reg_[i]);
}

void Pr31x00LcdGrayScale::RestoreState(StateReader& r) {
    for (uint32_t i = 0; i < kRegs; ++i) r.Read(kRegNames[i], reg_[i]);
}

REGISTER_SERVICE(Pr31x00LcdGrayScale);
