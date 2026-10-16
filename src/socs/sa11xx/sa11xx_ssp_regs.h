#pragma once

#include <cstdint>

namespace cerfcore {

namespace sa11xx_ssp {

/* SA-1110 Developer's Manual §11.12.9 SSCR0 (page 11-158): DSS 3..0, FRF 5..4, SSE 7, SCR 15..8. */
constexpr uint32_t kDssMask  = 0xFu;
constexpr uint32_t kFrfShift = 4;
constexpr uint32_t kSse      = 1u << 7;
constexpr uint32_t kScrShift = 8;

constexpr uint32_t kFrfMotorola  = 0u;
constexpr uint32_t kFrfMicrowire = 2u;

/* §11.12.10 SSCR1 (page 11-162): RIE 0, TIE 1, LBM 2, SPO 3, SPH 4, ECS 5. */
constexpr uint32_t kRie = 1u << 0;
constexpr uint32_t kTie = 1u << 1;
constexpr uint32_t kLbm = 1u << 2;
constexpr uint32_t kSpo = 1u << 3;
constexpr uint32_t kSph = 1u << 4;
constexpr uint32_t kEcs = 1u << 5;

/* §11.12.12 SSSR (page 11-165): TNF 1, RNE 2, BSY 3, TFS 4, RFS 5, ROR 6. */
constexpr uint32_t kTnf = 1u << 1;
constexpr uint32_t kRne = 1u << 2;
constexpr uint32_t kBsy = 1u << 3;
constexpr uint32_t kTfs = 1u << 4;
constexpr uint32_t kRfs = 1u << 5;
constexpr uint32_t kRor = 1u << 6;

/* §11.12.7.3 (page 11-155): "the transmit FIFO is 8 entries deep and the receive FIFO is 12 entries
   deep"; the DMA "burst size must be set to four half-words". */
constexpr uint32_t kTxDepth = 8u;
constexpr uint32_t kRxDepth = 12u;
constexpr uint32_t kBurst   = 4u;

/* SA-1110 Developer's Manual §9.2.1.1 (printed 9-12): IP19 "Serial port 4b", "SSP service
   request". */
constexpr uint32_t kIntcBitSsp = 1u << 19;

/* §9.1.2 (printed 9-9): GP 10 SSP_TXD, GP 11 SSP_RXD, GP 12 SSP_SCLK, GP 13 SSP_SFRM. */
constexpr uint32_t kTxdPin  = 10u;
constexpr uint32_t kRxdPin  = 11u;
constexpr uint32_t kSclkPin = 12u;
constexpr uint32_t kSfrmPin = 13u;

}

}
