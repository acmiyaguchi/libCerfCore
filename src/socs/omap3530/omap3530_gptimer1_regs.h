#pragma once

#include <cstdint>

namespace Omap3530Gptimer1Regs {

constexpr uint32_t kGptimer1BasePa = 0x48318000u;
constexpr uint32_t kGptimer1Size   = 0x00001000u;
/* SPRUF98Y Table 10-4 (printed p. 1052): M_IRQ_37 GPT1_IRQ. */
constexpr int      kIrqGptimer1    = 37;

constexpr uint32_t kOffTidr   = 0x00;
constexpr uint32_t kOffTiocp  = 0x10;
constexpr uint32_t kOffTistat = 0x14;
constexpr uint32_t kOffTisr   = 0x18;
constexpr uint32_t kOffTier   = 0x1C;
constexpr uint32_t kOffTwer   = 0x20;
constexpr uint32_t kOffTclr   = 0x24;
constexpr uint32_t kOffTcrr   = 0x28;
constexpr uint32_t kOffTldr   = 0x2C;
constexpr uint32_t kOffTtgr   = 0x30;
constexpr uint32_t kOffTwps   = 0x34;
constexpr uint32_t kOffTmar   = 0x38;
constexpr uint32_t kOffTcar1  = 0x3C;
constexpr uint32_t kOffTsicr  = 0x40;
constexpr uint32_t kOffTcar2  = 0x44;
constexpr uint32_t kOffTpir   = 0x48;
constexpr uint32_t kOffTnir   = 0x4C;
constexpr uint32_t kOffTcvr   = 0x50;
constexpr uint32_t kOffTocr   = 0x54;
constexpr uint32_t kOffTowr   = 0x58;

/* Table 16-18 (printed p. 2622-2623): TIOCP_CFG SOFTRESET [1], "This bit is
   automatically reset by the hardware. During reads, it always returns 0",
   "0x1: The module is reset." */
constexpr uint32_t kTiocpSoftReset = 1u << 1;

/* Table 16-42 (printed p. 2637): TSICR POSTED [2] RW reset 1; SFT [1] RW,
   "automatically reset by the hardware. During reads, it always returns 0";
   bits [31:3] and [0] read 0. */
constexpr uint32_t kTsicrSft    = 1u << 1;
constexpr uint32_t kTsicrPosted = 1u << 2;

/* Table 16-22 (printed p. 2625) TISR and Table 16-24 (printed p. 2626) TIER:
   TCAR [2], OVF [1], MAT [0], bits [31:3] read 0; a TISR write of 1 clears the
   flag and a write of 0 leaves it. */
constexpr uint32_t kIntMat  = 1u << 0;
constexpr uint32_t kIntOvf  = 1u << 1;
constexpr uint32_t kIntTcar = 1u << 2;
constexpr uint32_t kIntMask = kIntMat | kIntOvf | kIntTcar;

constexpr uint32_t kTclrSt  = 1u << 0;
constexpr uint32_t kTclrAr  = 1u << 1;
constexpr uint32_t kTclrPre = 1u << 5;
constexpr uint32_t kTclrCe  = 1u << 6;

constexpr uint32_t kTclrPinFields =
    (1u << 7) | (3u << 8) | (3u << 10) | (1u << 12) | (1u << 13) | (1u << 14);

/* TCLR field table (printed p. 2629): bits [31:15] are Reserved, "Reads
   return 0". */
constexpr uint32_t kTclrMask = 0x00007FFFu;

/* p. 2608 CAUTION: a TLDR of 0xFFFFFFFF "can lead to undesired results". */
constexpr uint32_t kTldrOverflowValue = 0xFFFFFFFFu;

constexpr uint64_t kCounterModulo = 0x100000000ull;

}
