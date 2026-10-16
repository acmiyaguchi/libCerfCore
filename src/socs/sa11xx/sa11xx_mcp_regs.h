#pragma once

#include <cstdint>

namespace sa11xx_mcp {

/* SA-1110 Developer's Manual §11.12.1.1 (printed 11-128): "Each MCP data frame is 128 bits
   long"; "After 64 SCLK cycles elapse ... The MCP takes the data from each field and places it in
   its respective receive FIFO". */
constexpr uint64_t kFrameTicks     = 128u;
constexpr uint64_t kSubframe0Ticks = 64u;

/* §11.12.1.3 (printed 11-131): "the audio and telecom transmit FIFOs are 8-entries deep and the
   audio and telecom receive FIFOs are 12-entries deep"; MCSR (printed 11-149): ATS at "four or
   fewer entries filled", ARS at "four or more entries filled"; Table 11-1: an 8-byte burst. */
constexpr uint32_t kTxDepth   = 8u;
constexpr uint32_t kRxDepth   = 12u;
constexpr uint32_t kRequestAt = 4u;
constexpr uint32_t kBurst     = 4u;

/* SA-1110 Developer's Manual §11.12.3 MCCR0 (printed 11-137/138): ASD 6:0, TSD 14:8, MCE 16, ECS 17,
   ADM 18, TTE 19, TRE 20, ATE 21, ARE 22, LBM 23, ECP 25:24; §11.12.4 MCCR1: CFS 20. */
constexpr uint32_t kDivisorMask = 0x7Fu;
constexpr uint32_t kTsdShift    = 8;
constexpr uint32_t kMce     = 1u << 16;
constexpr uint32_t kEcs     = 1u << 17;
constexpr uint32_t kAdm     = 1u << 18;
constexpr uint32_t kEcpShift = 24;
constexpr uint32_t kEcpMask  = 0x3u;
constexpr uint32_t kTte     = 1u << 19;
constexpr uint32_t kTre     = 1u << 20;
constexpr uint32_t kAte     = 1u << 21;
constexpr uint32_t kAre     = 1u << 22;
constexpr uint32_t kLbm     = 1u << 23;
constexpr uint32_t kCfs     = 1u << 20;

/* §11.12.6 MCSR (printed 11-149/150): ATS 0, ARS 1, TTS 2, TRS 3, ATU 4, ARO 5, TTU 6, TRO 7,
   ANF 8, ANE 9, TNF 10, TNE 11, ACE 14, TCE 15. */
constexpr uint32_t kAts = 1u << 0;
constexpr uint32_t kArs = 1u << 1;
constexpr uint32_t kTts = 1u << 2;
constexpr uint32_t kTrs = 1u << 3;
constexpr uint32_t kAtu = 1u << 4;
constexpr uint32_t kAro = 1u << 5;
constexpr uint32_t kTtu = 1u << 6;
constexpr uint32_t kTro = 1u << 7;
constexpr uint32_t kAnf = 1u << 8;
constexpr uint32_t kAne = 1u << 9;
constexpr uint32_t kTnf = 1u << 10;
constexpr uint32_t kTne = 1u << 11;
constexpr uint32_t kAce = 1u << 14;
constexpr uint32_t kTce = 1u << 15;

/* SA-1110 Developer's Manual §11.12.5.3 MCDR2 (printed 11-143 to 11-145): data 15:0, R/W 16,
   codec register address 20:17; §11.12.6.13 / §11.12.6.14 MCSR CWC 12, CRC 13. */
constexpr uint32_t kMcdr2Write     = 1u << 16;
constexpr uint32_t kMcdr2AddrShift = 17;
constexpr uint32_t kCwc            = 1u << 12;
constexpr uint32_t kCrc            = 1u << 13;

}
