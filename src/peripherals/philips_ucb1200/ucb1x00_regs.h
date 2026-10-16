#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

namespace ucb1x00_regs {

constexpr uint8_t kRegIoData   = 0x00;
constexpr uint8_t kRegIoDir    = 0x01;

/* UCB1300 datasheet p.49: IO_DATA / IO_DIR bits 0-9; a read of IO_DATA returns "the
   actual state of the associated I/O pin", an output pin carrying its written bit. */
constexpr uint16_t kIoPins = 0x03FFu;
constexpr uint8_t kRegIeRis    = 0x02;
constexpr uint8_t kRegIeFal    = 0x03;
constexpr uint8_t kRegIeStatus = 0x04;
constexpr uint8_t kRegTsCr     = 0x09;
constexpr uint8_t kRegAdcCr    = 0x0A;
constexpr uint8_t kRegAdcData  = 0x0B;
constexpr uint8_t kRegId       = 0x0C;
constexpr uint8_t kRegNull     = 0x0F;

/* NetBSD hpcmips ucb1200reg.h UCB1200_NULL_REG - "always returns 0xffff". */
constexpr uint16_t kNullValue = 0xFFFFu;

/* UCB1300 datasheet p.49-52, UCB1200 p.48-51, UCB1100 PDF index 29-32: the W and
   R/W bits of each register, the same on the three parts. */
constexpr std::array<uint16_t, 16> kDefinedWriteBits = {
    0x03FFu, 0x83FFu, 0xFBFFu, 0xFBFFu, 0xFBFFu, 0x00FFu, 0xE858u, 0x0FFFu,
    0xE15Fu, 0x0BFFu, 0x80BFu, 0x0000u, 0x0000u, 0xF03Fu, 0x0000u, 0x0000u,
};

/* UCB1300 datasheet, control register overview (p.49): IE bit 12 TSPX, bit 13 TSMX. */
constexpr uint16_t kIeTouch = (1u << 12) | (1u << 13);

/* Linux ucb1x00.h UCB_IO_0..9 and UCB_IE_ADC..UCB_IE_ACLIP (bits 11-15) list every
   interrupt source; ucb1x00-core.c ucb1x00_detect_irq writes 0xffff to UCB_IE_CLEAR. */
constexpr uint16_t kIeClearNoSource = 1u << 10;

constexpr uint16_t kTsCrModeMask = 3u << 8;
constexpr uint16_t kTsCrModePres = 1u << 8;
constexpr uint16_t kTsCrTspxLow  = 1u << 12;
constexpr uint16_t kTsCrTsmxLow  = 1u << 13;
constexpr uint16_t kTsCrBiasEna  = 1u << 11;

constexpr uint16_t kAdcStart      = 1u << 7;
constexpr uint16_t kAdcVrefbypCon = 1u << 1;
constexpr uint16_t kAdcExtRefEna  = 1u << 5;
constexpr uint16_t kAdcEna        = 1u << 15;
constexpr uint16_t kAdcDatVal  = 1u << 15;
constexpr uint16_t kAdcInpMask = 7u << 2;
constexpr uint16_t kAdcInpTspx = 0u << 2;
constexpr uint16_t kAdcInpTsmx = 1u << 2;
constexpr uint16_t kAdcInpTspy = 2u << 2;
constexpr uint16_t kAdcInpTsmy = 3u << 2;
constexpr uint16_t kAdcInpAd0  = 4u << 2;

constexpr uint16_t kAdcDataShift = 5;
constexpr uint16_t kAdcDataMask  = 0x3FFu;

constexpr uint8_t kRegTelCtlB = 0x06;
constexpr uint8_t kRegAudCtlB = 0x08;
constexpr uint8_t kRegMode    = 0x0D;

/* UCB1300 datasheet p.50: TEL_CLIP_STAT reg 6 bit 4, AUD_CLIP_STAT reg 8 bit 6, set by
   the clip detector on the codec input path. */
constexpr uint16_t kTelClipStat = 1u << 4;
constexpr uint16_t kAudClipStat = 1u << 6;

/* UCB1300 datasheet p.50, UCB1200 p.49, UCB1100 Address 6 / 8: TEL_IN_ENA<14> "the telecom input
   path is activated", AUD_MUTE<13> "the audio output is muted", AUD_IN_ENA<14> "the audio codec
   input path is activated", AUD_OUT_ENA<15> "the audio codec output path is activated". */
constexpr uint16_t kTelInEna  = 1u << 14;
constexpr uint16_t kAudMute   = 1u << 13;
constexpr uint16_t kAudInEna  = 1u << 14;
constexpr uint16_t kAudOutEna = 1u << 15;

}

}
