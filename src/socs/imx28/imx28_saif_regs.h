#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx28_saif {

/* Section 35.3 memory map. */
constexpr uint32_t kCtrl    = 0x00u;
constexpr uint32_t kStat    = 0x10u;
constexpr uint32_t kData    = 0x20u;
constexpr uint32_t kVersion = 0x30u;
constexpr uint32_t kMapEnd  = 0x40u;

/* Section 35.3.1 CTRL: reset C000_0000h; RSRVD2 [23:21]. */
constexpr uint32_t kCtrlReset    = 0xC0000000u;
constexpr uint32_t kCtrlReserved = 0x00E00000u;

/* Section 35.3.1 FIFO_ERROR_IRQ_EN [25], FIFO_SERVICE_IRQ_EN [24], CHANNEL_NUM_SELECT [15:14]
   (11 reserved), SLAVE_MODE [2], READ_MODE [1], RUN [0]. */
constexpr uint32_t kCtrlIrqEnables   = 0x03000000u;
constexpr uint32_t kCtrlChannelMask  = 0x0000C000u;
constexpr uint32_t kCtrlChannelRsvd  = 0x0000C000u;
constexpr uint32_t kCtrlReadMode     = 1u << 1;
constexpr uint32_t kCtrlSlaveMode    = 1u << 2;
constexpr uint32_t kCtrlRun          = 1u << 0;

/* Section 35.3.1 BITCLK_BASE_RATE [26], WORD_LENGTH [7:4] 0000 16-bit; section 35.2.1 saif_clk
   is 512 or 384 times the sample rate. */
constexpr uint32_t kCtrlBaseRate   = 1u << 26;
constexpr uint32_t kCtrlWordLength = 0x000000F0u;

/* Section 35.3.2 STAT: reset 8000_0000h; DMA_PREQ [16], FIFO_UNDERFLOW_IRQ [6],
   FIFO_OVERFLOW_IRQ [5], FIFO_SERVICE_IRQ [4], BUSY [0]. */
constexpr uint32_t kStatPresent   = 1u << 31;
constexpr uint32_t kStatDmaPreq   = 1u << 16;
constexpr uint32_t kStatUnderflow = 1u << 6;
constexpr uint32_t kStatOverflow  = 1u << 5;
constexpr uint32_t kStatService   = 1u << 4;
constexpr uint32_t kStatBusy      = 1u << 0;
constexpr uint32_t kStatClearable = kStatUnderflow | kStatOverflow | kStatService;

/* Table 7-1: APBX channel 4 SAIF0, channel 5 SAIF1. */
constexpr uint32_t kDmaChannelBase = 4u;

/* Section 35.2.2: 16-bit sample pairs, one FIFO entry per frame. */
constexpr uint32_t kFrameBytes = 4u;

/* Section 35.2.5: four-entry FIFO per channel pair. */
constexpr uint32_t kRxFifoEntries = 4u;

}  /* namespace imx28_saif */

}
