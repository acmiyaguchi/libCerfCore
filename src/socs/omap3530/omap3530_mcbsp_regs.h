#pragma once

#include "omap3530_mcbsp_frame.h"

#include <cstdint>

namespace cerfcore {

struct Omap3530McBspRegs {
    uint32_t spcr2  = 0;
    uint32_t spcr1  = 0;
    uint32_t pcr    = 0;
    uint32_t srgr1  = 0;
    uint32_t srgr2  = 0;
    uint32_t thrsh2 = 0;
    uint32_t thrsh1 = 0;
    uint32_t xccr   = 0;
    uint32_t rccr   = 0;
    Omap3530McBspFrame::Regs frame;
};

namespace omap3530_mcbsp {

/* OMAP35x TRM SPRUF98Y Table 21-48 SPCR2, Table 21-50 SPCR1, Table 21-76 PCR, Table 21-60 SRGR2,
   Table 21-62 SRGR1, Table 21-122 XCCR, Table 21-124 RCCR bit positions. */
constexpr uint32_t kSpcr2Xrst      = 1u << 0;
constexpr uint32_t kSpcr2Grst      = 1u << 6;
constexpr uint32_t kSpcr2Frst      = 1u << 7;
constexpr uint32_t kSpcr1Rrst      = 1u << 0;
constexpr uint32_t kSpcr1Alb       = 1u << 15;
constexpr uint32_t kPcrFsrp        = 1u << 2;
constexpr uint32_t kPcrFsxp        = 1u << 3;
constexpr uint32_t kPcrSclkme      = 1u << 7;
constexpr uint32_t kPcrClkrm       = 1u << 8;
constexpr uint32_t kPcrClkxm       = 1u << 9;
constexpr uint32_t kPcrFsrm        = 1u << 10;
constexpr uint32_t kPcrFsxm        = 1u << 11;
constexpr uint32_t kPcrRioen       = 1u << 12;
constexpr uint32_t kPcrXioen       = 1u << 13;
constexpr uint32_t kSrgr2Fsgm      = 1u << 12;
constexpr uint32_t kSrgr2Clksm     = 1u << 13;
constexpr uint32_t kSrgr2Gsync     = 1u << 15;
constexpr uint32_t kXccrXdmaen     = 1u << 3;
constexpr uint32_t kRccrRdisable   = 1u << 0;
constexpr uint32_t kRccrRdmaen     = 1u << 3;
constexpr uint32_t kRccrRfullCycle = 1u << 11;

inline uint32_t IntMode(uint32_t spcr) { return (spcr >> 4) & 3u; }
inline uint32_t Rjust(uint32_t spcr1) { return (spcr1 >> 13) & 3u; }
inline uint32_t Fper(uint32_t srgr2) { return srgr2 & 0xFFFu; }
inline uint32_t Fwid(uint32_t srgr1) { return (srgr1 >> 8) & 0xFFu; }
inline uint32_t Clkgdv(uint32_t srgr1) { return srgr1 & 0xFFu; }

}

}
