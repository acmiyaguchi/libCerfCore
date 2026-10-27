#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx28_pxp {

/* MCIMX28RM Rev. 2 section 34.4 memory map: one register every 0x10 bytes, 0x000 to 0x3F0. */
constexpr uint32_t kSlots = 0x40u;

constexpr uint32_t Slot(uint32_t offset) { return offset >> 4; }

constexpr uint32_t kCtrl         = 0x000u;
constexpr uint32_t kStat         = 0x010u;
constexpr uint32_t kOutbuf       = 0x020u;
constexpr uint32_t kOutsize      = 0x040u;
constexpr uint32_t kS0buf        = 0x050u;
constexpr uint32_t kS0ubuf       = 0x060u;
constexpr uint32_t kS0vbuf       = 0x070u;
constexpr uint32_t kS0param      = 0x080u;
constexpr uint32_t kS0background = 0x090u;
constexpr uint32_t kS0crop       = 0x0A0u;
constexpr uint32_t kS0scale      = 0x0B0u;
constexpr uint32_t kS0offset     = 0x0C0u;
constexpr uint32_t kCsccoeff0    = 0x0D0u;
constexpr uint32_t kCsccoeff1    = 0x0E0u;
constexpr uint32_t kCsccoeff2    = 0x0F0u;
constexpr uint32_t kNext         = 0x100u;
constexpr uint32_t kS0ColorkeyLo = 0x180u;
constexpr uint32_t kS0ColorkeyHi = 0x190u;
constexpr uint32_t kDebugctrl    = 0x1D0u;
constexpr uint32_t kDebug        = 0x1E0u;
constexpr uint32_t kVersion      = 0x1F0u;
constexpr uint32_t kOl0          = 0x200u;
constexpr uint32_t kOlStride     = 0x040u;
constexpr uint32_t kOlParam      = 0x020u;
constexpr uint32_t kOverlays     = 8u;

struct RegSpec {
    uint32_t reset;
    uint32_t writable;
};

/* Sections 34.4.1 to 34.4.56 reset values and writable fields; {0, 0} marks an address with no
   register. */
constexpr RegSpec SpecAt(uint32_t offset) {
    switch (offset) {
        case kCtrl:          return {0xC0000000u, 0xCFECFFF7u};
        case kStat:          return {0x00000000u, 0x0000000Fu};
        case 0x020u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x030u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x040u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x050u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x060u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x070u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x080u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x090u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x0A0u:         return {0x00000000u, 0xFFFFFFFFu};
        case 0x0B0u:         return {0x10001000u, 0x7FFF7FFFu};
        case 0x0C0u:         return {0x00000000u, 0x0FFF0FFFu};
        case kCsccoeff0:     return {0x04000000u, 0x9FFFFFFFu};
        case kCsccoeff1:     return {0x01230208u, 0x07FF07FFu};
        case kCsccoeff2:     return {0x079B076Cu, 0x07FF07FFu};
        case kNext:          return {0x00000000u, 0xFFFFFFFCu};
        case kS0ColorkeyLo:  return {0x00FFFFFFu, 0x00FFFFFFu};
        case kS0ColorkeyHi:  return {0x00000000u, 0x00FFFFFFu};
        case 0x1A0u:         return {0x00FFFFFFu, 0x00FFFFFFu};
        case 0x1B0u:         return {0x00000000u, 0x00FFFFFFu};
        case kDebugctrl:     return {0x00000000u, 0x000000FFu};
        case kDebug:         return {0x00000000u, 0x00000000u};
        case kVersion:       return {0x02000000u, 0x00000000u};
        default: break;
    }
    if (offset < kOl0 || offset >= kOl0 + kOverlays * kOlStride) return {0u, 0u};
    switch (offset & (kOlStride - 1u)) {
        case 0x00u:    return {0x00000000u, 0xFFFFFFFFu};
        case 0x10u:    return {0x00000000u, 0xFFFFFFFFu};
        case kOlParam: return {0x00000000u, 0x000FFFFFu};
        default:       return {0x00000000u, 0x00000000u};
    }
}

/* Section 34.4.1 CTRL. */
constexpr uint32_t kCtrlEnable        = 1u << 0;
constexpr uint32_t kCtrlIrqEnable     = 1u << 1;
constexpr uint32_t kCtrlNextIrqEnable = 1u << 2;
constexpr uint32_t kCtrlOutFormat     = 0xFu << 4;
constexpr uint32_t kCtrlRotateShift   = 8u;
constexpr uint32_t kCtrlHflip         = 1u << 10;
constexpr uint32_t kCtrlVflip         = 1u << 11;
constexpr uint32_t kCtrlS0Format      = 0xFu << 12;
constexpr uint32_t kCtrlScale         = 1u << 18;
constexpr uint32_t kCtrlCrop          = 1u << 19;
constexpr uint32_t kCtrlInPlace       = 1u << 21;
constexpr uint32_t kCtrlBlockSize     = 1u << 23;
constexpr uint32_t kCtrlInterlaced    = 0xFu << 24;
constexpr uint32_t kFormatRgb565      = 0x4u;
constexpr uint32_t kFormatYuv420      = 0x9u;

/* Section 34.4.2 STAT: IRQ [0], NEXT_IRQ [3]. */
constexpr uint32_t kStatIrq     = 1u << 0;
constexpr uint32_t kStatNextIrq = 1u << 3;

/* Section 34.4.17 NEXT ENABLED [0]. */
constexpr uint32_t kNextEnabled = 1u << 0;

/* Section 34.2.12 Table 34-3: the reload structure holds 48 words, CTRL to OL7PARAM2. */
constexpr uint32_t kReloadWords = 48u;

}

}
