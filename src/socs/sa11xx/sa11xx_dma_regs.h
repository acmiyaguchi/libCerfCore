#pragma once

#include "sa11xx_dma_stream.h"

#include <cstdint>

namespace cerfcore {

namespace sa11xx_dma {

constexpr uint32_t kIrqLevelBits = kDoneA | kDoneB | kError;

constexpr uint32_t kOffDdar     = 0x00;
constexpr uint32_t kOffDcsrSet  = 0x04;
constexpr uint32_t kOffDcsrClr  = 0x08;
constexpr uint32_t kOffDcsrRo   = 0x0C;
constexpr uint32_t kOffDbsa     = 0x10;
constexpr uint32_t kOffDbta     = 0x14;
constexpr uint32_t kOffDbsb     = 0x18;
constexpr uint32_t kOffDbtb     = 0x1C;

constexpr uint32_t kIntcBitDmaCh0 = 20;

/* SA-1110 Developer's Manual §11.6.1.1 DDARn: RW 0, E 1, BS 2, DW 3, DS 7:4, DA 31:8. */
constexpr uint32_t kDdarRw = 1u << 0;
constexpr uint32_t kDdarE  = 1u << 1;
constexpr uint32_t kDdarBs = 1u << 2;
constexpr uint32_t kDdarDw = 1u << 3;

/* §11.6.1.1 (printed 11-7): DDAR reset row 0 in bits 31..26; §11.6.1.4 / §11.6.1.6 (printed
   11-12): DBTx bits 31..13 "are reserved and read as zeros. Writes to this field have no effect". */
constexpr uint32_t kDdarResetKeep = 0x03FFFFFFu;
constexpr uint32_t kDbtMask       = 0x00001FFFu;

/* §11.6.1.2 (printed 11-10 / 11-11): DCSR bits 31..8 "Writes to this field have no effect"; BIU
   "is never cleared except on reset (hardware, software, or sleep)". */
constexpr uint32_t kDcsrWritable  = 0x000000FFu;
constexpr uint32_t kDcsrClearable = kRun | kIe | kError | kDoneA | kStrtA | kDoneB | kStrtB;

}

}
