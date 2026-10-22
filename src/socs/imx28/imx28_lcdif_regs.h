#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 33.4 memory map; reset values and fields from sections 33.4.1 to
   33.4.32. */
struct Imx28LcdifRegSpec {
    uint32_t offset;
    uint32_t reset;
    uint32_t writable;
    bool     sct;
};

namespace imx28_lcdif {

constexpr uint32_t kCtrl          = 0x000u;
constexpr uint32_t kCtrl1         = 0x010u;
constexpr uint32_t kCtrl2         = 0x020u;
constexpr uint32_t kTransferCount = 0x030u;
constexpr uint32_t kCurBuf        = 0x040u;
constexpr uint32_t kNextBuf       = 0x050u;
constexpr uint32_t kVdctrl0       = 0x070u;
constexpr uint32_t kData          = 0x180u;
constexpr uint32_t kStat          = 0x1B0u;
constexpr uint32_t kVersion       = 0x1C0u;
constexpr uint32_t kMapEnd        = 0x200u;

constexpr Imx28LcdifRegSpec kStorage[] = {
    {kCtrl,          0xC0000000u, 0xFFFFFFAFu, true },
    {kCtrl1,         0x000F0000u, 0x0FFFFF07u, true },
    {kCtrl2,         0x00200000u, 0x00F7777Eu, true },
    {kTransferCount, 0x00010000u, 0xFFFFFFFFu, false},
    {kCurBuf,        0x00000000u, 0xFFFFFFFFu, false},
    {kNextBuf,       0x00000000u, 0xFFFFFFFFu, false},
    {0x060u,         0x00000000u, 0xFFFFFFFFu, false},
    {kVdctrl0,       0x00000000u, 0x3F3FFFFFu, true },
    {0x080u,         0x00000000u, 0xFFFFFFFFu, false},
    {0x090u,         0x00000000u, 0xFFFFFFFFu, false},
    {0x0A0u,         0x00000000u, 0x3FFFFFFFu, false},
    {0x0B0u,         0x00000000u, 0xE007FFFFu, false},
    {0x0C0u,         0x00000000u, 0x0FFF0FFFu, false},
    {0x0D0u,         0x00000000u, 0x3FFFFFFFu, false},
    {0x0E0u,         0x00000000u, 0x3FFFFFFFu, false},
    {0x0F0u,         0x00000000u, 0x3FFFFFFFu, false},
    {0x100u,         0x00000000u, 0xFFFFFFFFu, false},
    {0x110u,         0x00000000u, 0x03FF0003u, false},
    {0x120u,         0x00000000u, 0x03FF03FFu, false},
    {0x130u,         0x00000000u, 0x03FF03FFu, false},
    {0x140u,         0x00000000u, 0x03FF03FFu, false},
    {0x150u,         0x00000000u, 0x03FF03FFu, false},
    {0x160u,         0x00800010u, 0x01FF01FFu, false},
    {0x170u,         0x00FF00FFu, 0xFFFFFFFFu, false},
};

constexpr uint32_t kStorageCount = sizeof(kStorage) / sizeof(kStorage[0]);

/* Section 33.4.28 STAT reset 9500_0000h; section 33.4.29 VERSION reset 0400_0000h. */
constexpr uint32_t kStatIdle     = 0x95000000u;
constexpr uint32_t kVersionValue = 0x04000000u;

/* Section 33.4.1 CTRL. */
constexpr uint32_t kCtrlRun              = 1u << 0;
constexpr uint32_t kCtrlDataFormat16     = 1u << 3;
constexpr uint32_t kCtrlMaster           = 1u << 5;
constexpr uint32_t kCtrlRgbToYcbcr       = 1u << 7;
constexpr uint32_t kCtrlWordLength       = 3u << 8;
constexpr uint32_t kCtrlDatabusWidth     = 3u << 10;
constexpr uint32_t kCtrlCscSwizzle       = 3u << 12;
constexpr uint32_t kCtrlInputSwizzle     = 3u << 14;
constexpr uint32_t kCtrlDataSelect       = 1u << 16;
constexpr uint32_t kCtrlDotclkMode       = 1u << 17;
constexpr uint32_t kCtrlVsyncMode        = 1u << 18;
constexpr uint32_t kCtrlBypassCount      = 1u << 19;
constexpr uint32_t kCtrlDviMode          = 1u << 20;
constexpr uint32_t kCtrlShiftNumBits     = 0x1Fu << 21;
constexpr uint32_t kCtrlWaitForVsync     = 1u << 27;
constexpr uint32_t kCtrlReadWriteb       = 1u << 28;
constexpr uint32_t kCtrlYcbcr422Input    = 1u << 29;

/* Section 33.4.2 CTRL1. */
constexpr uint32_t kCtrl1Reset           = 1u << 0;
constexpr uint32_t kCtrl1BusyEnable      = 1u << 2;
constexpr uint32_t kCtrl1IrqStatus       = 0x02000F00u;
constexpr uint32_t kCtrl1FrameDoneIrq    = 1u << 9;
constexpr uint32_t kCtrl1PackingShift    = 16u;
constexpr uint32_t kCtrl1Packing         = 0xFu << kCtrl1PackingShift;
constexpr uint32_t kCtrl1FifoClear       = 1u << 21;
constexpr uint32_t kCtrl1InterlaceFields = 1u << 23;

/* Section 33.4.3 CTRL2: ODD_LINE_PATTERN [18:16], EVEN_LINE_PATTERN [14:12]. */
constexpr uint32_t kCtrl2LinePatterns = 0x00077000u;

/* Section 33.4.8 VDCTRL0 VSYNC_OEB [29]. */
constexpr uint32_t kVdctrl0VsyncOeb = 1u << 29;

}

}
