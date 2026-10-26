#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx28_i2c {

/* MCIMX28RM Rev. 2 § 27.5 memory map. */
constexpr uint32_t kCtrl0      = 0x000u;
constexpr uint32_t kTiming0    = 0x010u;
constexpr uint32_t kTiming1    = 0x020u;
constexpr uint32_t kTiming2    = 0x030u;
constexpr uint32_t kCtrl1      = 0x040u;
constexpr uint32_t kStat       = 0x050u;
constexpr uint32_t kQueueCtrl  = 0x060u;
constexpr uint32_t kQueueStat  = 0x070u;
constexpr uint32_t kQueueCmd   = 0x080u;
constexpr uint32_t kQueueData  = 0x090u;
constexpr uint32_t kData       = 0x0A0u;
constexpr uint32_t kDebug0     = 0x0B0u;
constexpr uint32_t kDebug1     = 0x0C0u;
constexpr uint32_t kVersion    = 0x0D0u;
constexpr uint32_t kMapEnd     = 0x0E0u;

/* § 27.5 memory map, Reset value column. */
constexpr uint32_t kCtrl0Reset     = 0xC0000000u;
constexpr uint32_t kTiming0Reset   = 0x00780030u;
constexpr uint32_t kTiming1Reset   = 0x00800030u;
constexpr uint32_t kTiming2Reset   = 0x00300030u;
constexpr uint32_t kCtrl1Reset     = 0x08860000u;
constexpr uint32_t kQueueStatReset = 0x00002020u;
constexpr uint32_t kDebug0Reset    = 0x00100000u;
constexpr uint32_t kDebug1Reset    = 0xC0000000u;
constexpr uint32_t kVersionValue   = 0x01040000u;

/* § 27.5.1 CTRL0. */
constexpr uint32_t kCtrl0Run         = 1u << 29;
constexpr uint32_t kCtrl0Rsvd2       = 1u << 28;
constexpr uint32_t kCtrl0PreAck      = 1u << 27;
constexpr uint32_t kCtrl0Acknowledge = 1u << 26;
constexpr uint32_t kCtrl0Rsvd1       = 1u << 24;
constexpr uint32_t kCtrl0MultiMaster = 1u << 23;
constexpr uint32_t kCtrl0ClockHeld   = 1u << 22;
constexpr uint32_t kCtrl0Retain      = 1u << 21;
constexpr uint32_t kCtrl0PostStop    = 1u << 20;
constexpr uint32_t kCtrl0PreStart    = 1u << 19;
constexpr uint32_t kCtrl0SlaveEnable = 1u << 18;
constexpr uint32_t kCtrl0Master      = 1u << 17;
constexpr uint32_t kCtrl0Transmit    = 1u << 16;
constexpr uint32_t kCtrl0CountMask   = 0xFFFFu;
constexpr uint32_t kCtrl0Unsupported = kCtrl0Rsvd2 | kCtrl0PreAck | kCtrl0Acknowledge |
                                       kCtrl0Rsvd1 | kCtrl0MultiMaster | kCtrl0SlaveEnable;

/* § 27.5.2-27.5.4: RSVD2 [31:26] and RSVD1 [15:10] are always zero. */
constexpr uint32_t kTimingMask = 0x03FF03FFu;

/* § 27.5.5 CTRL1. */
constexpr uint32_t kCtrl1Rsvd1       = 1u << 31;
constexpr uint32_t kCtrl1QueueIrqs   = 3u << 29;
constexpr uint32_t kCtrl1ClrGotNak   = 1u << 28;
constexpr uint32_t kCtrl1NoSlaveAck  = 1u << 5;
constexpr uint32_t kCtrl1ForceIdle   = 3u << 25;
constexpr uint32_t kCtrl1BcastEn     = 1u << 24;
constexpr uint32_t kCtrl1Unsupported = kCtrl1Rsvd1 | kCtrl1QueueIrqs | kCtrl1ForceIdle |
                                       kCtrl1BcastEn;
constexpr uint32_t kCtrl1StatusMask  = 0xFFu;
constexpr uint32_t kCtrl1EnableShift = 8u;
constexpr uint32_t kCtrl1BusFreeIrq  = 1u << 7;
constexpr uint32_t kCtrl1DataEngineCmplt = 1u << 6;

/* § 27.5.6 STAT. */
constexpr uint32_t kStatPresent     = 0xC0000000u;
constexpr uint32_t kStatAnyIrq      = 1u << 29;
constexpr uint32_t kStatGotNak      = 1u << 28;
constexpr uint32_t kStatDmaWait     = 1u << 12;
constexpr uint32_t kStatBusBusy     = 1u << 11;
constexpr uint32_t kStatEngineBusy  = 1u << 9;

/* § 27.5.12 DEBUG0 DMAREQ [31]. */
constexpr uint32_t kDebug0DmaReq = 1u << 31;

constexpr uint32_t kWordBytes = 4u;

inline bool HasSctAlias(uint32_t reg) {
    return reg == kCtrl0 || reg == kTiming0 || reg == kTiming1 || reg == kTiming2 ||
           reg == kCtrl1 || reg == kQueueCtrl || reg == kQueueCmd || reg == kDebug0 ||
           reg == kDebug1;
}

}  // namespace imx28_i2c

}
