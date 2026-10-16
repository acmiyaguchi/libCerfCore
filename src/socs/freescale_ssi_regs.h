#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX51RM Table 56-12 SCR, Figure 56-28 SISR, Figure 56-29 SIER, Tables 56-15 / 56-16 STCR / SRCR,
   Figure 56-34 SFCSR, Figure 56-23 SACNT; MCIMX31RM Table 45-6 (pages 45-23 / 45-24): the same bit
   positions. */
namespace cerf_freescale_ssi {

constexpr uint32_t kScrSsien    = 1u << 0;
constexpr uint32_t kScrTe       = 1u << 1;
constexpr uint32_t kScrRe       = 1u << 2;
constexpr uint32_t kScrNet      = 1u << 3;
constexpr uint32_t kScrSyn      = 1u << 4;
constexpr uint32_t kScrI2sShift = 5;
constexpr uint32_t kI2sMaster   = 1u;
constexpr uint32_t kI2sSlave    = 2u;
constexpr uint32_t kScrTchEn    = 1u << 8;

constexpr uint32_t kSierTie   = 1u << 19;
constexpr uint32_t kSierTdmae = 1u << 20;
constexpr uint32_t kSierRie   = 1u << 21;
constexpr uint32_t kSierRdmae = 1u << 22;

constexpr uint32_t kCrDir            = 1u << 5;
constexpr uint32_t kCrFdir           = 1u << 6;
constexpr uint32_t kCrFen0           = 1u << 7;

constexpr uint32_t kSisrTfe0 = 1u << 0;
constexpr uint32_t kSisrRff0 = 1u << 2;
constexpr uint32_t kSisrRls  = 1u << 4;
constexpr uint32_t kSisrTls  = 1u << 5;
constexpr uint32_t kSisrRfs  = 1u << 6;
constexpr uint32_t kSisrTfs  = 1u << 7;
constexpr uint32_t kSisrTue0 = 1u << 8;
constexpr uint32_t kSisrRoe0 = 1u << 10;
constexpr uint32_t kSisrTde0 = 1u << 12;
constexpr uint32_t kSisrRdr0 = 1u << 14;
constexpr uint32_t kSisrTfrc = 1u << 23;
constexpr uint32_t kSisrRfrc = 1u << 24;

constexpr uint32_t kSacntAc97en     = 1u << 0;
constexpr uint32_t kSacntFv         = 1u << 1;
constexpr uint32_t kSacntRd         = 1u << 3;
constexpr uint32_t kSacntWr         = 1u << 4;
constexpr uint32_t kSacntFrdivShift = 5;
constexpr uint32_t kSacntFrdivMask  = 0x3Fu;

constexpr uint32_t kSfcsrTfwm  = 0;
constexpr uint32_t kSfcsrRfwm  = 4;
constexpr uint32_t kSfcsrTfcnt = 8;
constexpr uint32_t kSfcsrRfcnt = 12;

constexpr uint32_t Fifo(uint32_t bit0, uint32_t k) { return bit0 << k; }
constexpr uint32_t CrFen(uint32_t k) { return kCrFen0 << k; }
constexpr uint32_t SfcsrField(uint32_t sfcsr, uint32_t field, uint32_t k) {
    return (sfcsr >> (field + 16u * k)) & 0xFu;
}

}

}
