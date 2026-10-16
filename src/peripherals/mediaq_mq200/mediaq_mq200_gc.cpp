#include "mediaq_mq200_gc.h"

#include "mediaq_mq200_power.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace {

constexpr uint32_t kGcBlockLo      = 0xA000u;
constexpr uint32_t kGcBlockHi      = 0xC000u;
constexpr uint32_t kGcRegsHi       = 0xA100u;
constexpr uint32_t kGcStride       = 0x80u;
constexpr uint32_t kPaletteLo      = 0x10000u;
constexpr uint32_t kPaletteHi      = 0x10400u;
constexpr uint32_t kPaletteBlockHi = 0x12000u;

constexpr uint32_t kCrtOrCrc = 0x04u;
constexpr uint32_t kHSync    = 0x10u;
constexpr uint32_t kVSync    = 0x14u;
constexpr uint32_t kAltHWin  = 0x28u;
constexpr uint32_t kAltVWin  = 0x2Cu;
constexpr uint32_t kAltStart = 0x34u;

/* MQ-200 Data Book Table 5-8 .. Table 5-29, Table 5-32. */
constexpr uint32_t kCtrlUnmodeled      = 0x008C0606u;
constexpr uint32_t kCtrlAltWindow      = 1u << 11;
constexpr uint32_t kCtrlDepthShift     = 4u;
constexpr uint32_t kCtrlAltDepthShift  = 12u;
constexpr uint32_t kWinField           = 0xFFFu;
constexpr uint32_t kStartMask          = 0x001FFFFFu;
constexpr uint32_t kCtrlFdShift        = 20u;
constexpr uint32_t kCtrlFdReserved     = 7u;
constexpr uint32_t kGc01MustBeZero     = 0xE8000000u;
constexpr uint32_t kGc01ReadOnly       = 0x07000000u;
constexpr uint32_t kGc01MonitorSense   = 1u << 14;
constexpr uint32_t kGc01SenseNotLoaded = 0x07000000u;
constexpr uint32_t kTimingReserved     = 0xF000F000u;
constexpr uint32_t kHWinReserved       = 0x0000F000u;
constexpr uint32_t kStartReserved      = 0xFFE00000u;
constexpr uint32_t kAltStartReserved   = 0x01E00000u;
constexpr uint32_t kCursorAddrReserved = 0xC0C0F800u;
constexpr uint32_t kColorReserved      = 0xFF000000u;
constexpr uint32_t kPaletteImplemented = 0x00FFFFFFu;

constexpr uint32_t kDepth565Palette     = 0x4u;
constexpr uint32_t kDepthReservedLo     = 0x8u;
constexpr uint32_t kDepthReservedHi     = 0xBu;

bool DepthReserved(uint32_t depth) {
    return depth >= kDepthReservedLo && depth <= kDepthReservedHi;
}

}

void MediaQMq200Gc::PowerOnReset() {
    std::memset(regs_, 0, sizeof(regs_));
    std::memset(palette_, 0, sizeof(palette_));
}

bool MediaQMq200Gc::Owns(uint32_t roff) {
    return (roff >= kGcBlockLo && roff < kGcBlockHi) ||
           (roff >= kPaletteLo && roff < kPaletteBlockHi);
}

uint32_t MediaQMq200Gc::Read(uint32_t roff) const {
    if (roff >= kPaletteLo && roff < kPaletteHi) return palette_[(roff - kPaletteLo) / 4u];
    if (roff >= kGcBlockLo && roff < kGcRegsHi) {
        const uint32_t gc  = (roff - kGcBlockLo) / kGcStride;
        const uint32_t off = (roff - kGcBlockLo) % kGcStride;
        const bool readable = off <= kCursorBg && off != 0x18u && off != kVCount && off != 0x3Cu &&
                              !(gc == kGc2 && off == kCrtOrCrc);
        if (readable) {
            const uint32_t v = regs_[gc][off / 4u];
            if (off == kCrtOrCrc && (v & kGc01MonitorSense)) return v | kGc01SenseNotLoaded;
            return v;
        }
    }
    emu_.Get<Fatal>().Die("MediaQMq200Gc: read of reserved or unmodeled register 0x%05X", roff);
}

void MediaQMq200Gc::Write(uint32_t roff, uint32_t value) {
    if (roff >= kPaletteLo && roff < kPaletteHi) {
        palette_[(roff - kPaletteLo) / 4u] = value & kPaletteImplemented;
        return;
    }
    if (roff < kGcBlockLo || roff >= kGcRegsHi) {
        emu_.Get<Fatal>().Die("MediaQMq200Gc: write 0x%08X to reserved register 0x%05X", value,
                              roff);
    }
    const uint32_t gc  = (roff - kGcBlockLo) / kGcStride;
    const uint32_t off = (roff - kGcBlockLo) % kGcStride;
    uint32_t       reserved = 0u;
    switch (off) {
        case kCtrl:
            if ((value & kCtrlUnmodeled) || ((value >> kCtrlFdShift) & 7u) == kCtrlFdReserved ||
                DepthReserved((value >> 4) & 0xFu) || DepthReserved((value >> 12) & 0xFu)) {
                emu_.Get<Fatal>().Die("MediaQMq200Gc: GC%u control 0x%08X sets a counter reset, "
                                      "a test mode, a reserved bit, the reserved FD 111 or a "
                                      "reserved colour depth; not modeled", gc + 1u, value);
            }
            break;
        case kCrtOrCrc:
            if (gc == kGc2) {
                emu_.Get<Fatal>().Die("MediaQMq200Gc: GC21R CRC control 0x%08X is not modeled",
                                      value);
            }
            if ((value & kGc01MustBeZero) || ((value >> 4) & 3u) == 3u ||
                ((value >> 6) & 3u) == 3u) {
                emu_.Get<Fatal>().Die("MediaQMq200Gc: GC01R 0x%08X sets reserved bits or a "
                                      "reserved sync control", value);
            }
            value &= ~kGc01ReadOnly;
            break;
        case kHDisp:
        case kVDisp:
        case kHSync:
        case kVSync:
        case kVWin:
        case kAltVWin:
        case kCursorPos:
            reserved = kTimingReserved;
            break;
        case kHWin:
        case kAltHWin:    reserved = kHWinReserved; break;
        case kStart:      reserved = kStartReserved; break;
        case kAltStart:   reserved = kAltStartReserved; break;
        case kStride:     break;
        case kCursorAddr: reserved = kCursorAddrReserved; break;
        case kCursorFg:
        case kCursorBg:   reserved = kColorReserved; break;
        default:
            emu_.Get<Fatal>().Die("MediaQMq200Gc: write 0x%08X to reserved or read-only register "
                                  "0x%05X", value, roff);
    }
    if (value & reserved) {
        emu_.Get<Fatal>().Die("MediaQMq200Gc: write 0x%08X to 0x%05X sets reserved bits", value,
                              roff);
    }
    regs_[gc][off / 4u] = value;
}

bool MediaQMq200Gc::Displays(uint32_t gc) const {
    const uint32_t ctrl = regs_[gc][kCtrl / 4u];
    return (ctrl & kCtrlEnable) && (ctrl & kCtrlWindowEnable);
}

MediaQMq200Gc::Window MediaQMq200Gc::ActiveWindow(uint32_t gc,
                                                  const MediaQMq200Power& power) const {
    const uint32_t* r    = regs_[gc];
    const uint32_t  ctrl = r[kCtrl / 4u];
    const bool alt = (ctrl & kCtrlAltWindow) ||
                     power.StateEnables(gc == kGc1 ? MediaQMq200Power::kModAltWin1
                                                   : MediaQMq200Power::kModAltWin2);
    const uint32_t hwin   = r[(alt ? kAltHWin : kHWin) / 4u];
    const uint32_t vwin   = r[(alt ? kAltVWin : kVWin) / 4u];
    const uint32_t stride = r[kStride / 4u];
    Window win;
    win.depth  = (ctrl >> (alt ? kCtrlAltDepthShift : kCtrlDepthShift)) & 0xFu;
    win.x      = hwin & kWinField;
    win.w      = ((hwin >> 16) & kWinField) + 1u;
    win.y      = vwin & kWinField;
    win.h      = ((vwin >> 16) & kWinField) + 1u;
    win.start  = r[(alt ? kAltStart : kStart) / 4u] & kStartMask;
    win.stride = static_cast<int16_t>(static_cast<uint16_t>(alt ? stride >> 16 : stride));
    const uint32_t bits = DepthBits(win.depth);
    if (alt && DepthIndexed(win.depth) && bits < 8u) {
        win.palette_base = (r[kAltStart / 4u] >> 24) & ~((1u << bits) - 1u) & 0xFFu;
    }
    return win;
}

/* MQ-200 Data Book Table 5-8 (GCD / AGCD). */
uint32_t MediaQMq200Gc::DepthBits(uint32_t depth) {
    static constexpr uint8_t kBits[16] = {1, 2, 4, 8, 16, 24, 32, 32, 0, 0, 0, 0, 16, 24, 32, 32};
    return kBits[depth & 0xFu];
}

bool MediaQMq200Gc::DepthIndexed(uint32_t depth) {
    return depth < kDepth565Palette;
}

bool MediaQMq200Gc::DepthUsesPalette(uint32_t depth) {
    return depth < kDepthReservedLo;
}

uint32_t MediaQMq200Gc::DisplayWidth(uint32_t gc) const {
    return (regs_[gc][kHDisp / 4u] >> 16) & 0xFFFu;
}

uint32_t MediaQMq200Gc::DisplayHeight(uint32_t gc) const {
    return ((regs_[gc][kVDisp / 4u] >> 16) & 0xFFFu) + 1u;
}

MediaQMq200Gc::ScanTiming MediaQMq200Gc::Gc1ScanTiming(const MediaQMq200Power& power) const {
    ScanTiming     t;
    const uint32_t ctrl = regs_[kGc1][kCtrl / 4u];
    t.on = !power.InD3() && (ctrl & kCtrlEnable) && power.Powered(MediaQMq200Power::kModGc1);
    if (!t.on) return t;
    const uint32_t hd = regs_[kGc1][kHDisp / 4u];
    const uint32_t vd = regs_[kGc1][kVDisp / 4u];
    t.line  = (hd & 0xFFFu) + 2u;
    t.frame = t.line * ((vd & 0xFFFu) + 1u);
    t.vde   = t.line * (((vd >> 16) & 0xFFFu) + 1u);
    const uint64_t sd  = ctrl >> 24;
    const auto     clk = power.State((ctrl >> 16) & 3u);
    if (clk.kind == MediaQMq200Power::ClockState::Stopped || sd == 0u) return t;
    t.running = true;
    if (clk.kind == MediaQMq200Power::ClockState::Unmodeled) {
        t.why = clk.why;
        return t;
    }
    if (t.vde > t.frame) {
        t.why = "a GC1 display end past the frame total";
        return t;
    }
    const uint64_t fd        = (ctrl >> kCtrlFdShift) & 7u;
    const uint64_t fd_halves = fd == 0u ? 2u : 2u * fd + 1u;
    if (!clk.rate.ScaleFits(2u, fd_halves * sd)) {
        t.why = "a GC1 pixel clock whose ratio overflows 64 bits";
        return t;
    }
    t.rate = clk.rate.Scaled(2u, fd_halves * sd).Reduced();
    return t;
}

void MediaQMq200Gc::SaveState(StateWriter& w) const {
    w.WriteBytes("gc_regs", &regs_[0][0], sizeof(regs_));
    w.WriteBytes("palette", palette_, sizeof(palette_));
}

void MediaQMq200Gc::RestoreState(StateReader& r) {
    r.ReadBytes("gc_regs", &regs_[0][0], sizeof(regs_));
    r.ReadBytes("palette", palette_, sizeof(palette_));
}
