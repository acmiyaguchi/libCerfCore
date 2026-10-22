#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 22.8.7 PERSISTENT0: reset 0000_0100h. */
constexpr uint32_t kPersist0Reset     = 0x00000100u;
constexpr uint32_t kP0Clocksource     = 1u << 0;
constexpr uint32_t kP0AlarmWakeEn     = 1u << 1;
constexpr uint32_t kP0AlarmEn         = 1u << 2;
constexpr uint32_t kP0LckSecs         = 1u << 3;
constexpr uint32_t kP0Xtal24Pwrup     = 1u << 4;
constexpr uint32_t kP0Xtal32Pwrup     = 1u << 5;
constexpr uint32_t kP0Xtal32Freq      = 1u << 6;
constexpr uint32_t kP0AlarmWake       = 1u << 7;
constexpr uint32_t kP0MsecResShift    = 8u;
constexpr uint32_t kP0MsecResMask     = 0x1Fu;
constexpr uint32_t kP0MsecRes         = kP0MsecResMask << kP0MsecResShift;
constexpr uint32_t kP0Rsvd1           = 0x3Fu << 22;
constexpr uint32_t kP0AutoRestart     = 1u << 17;
constexpr uint32_t kMsecRes1ms        = 0x01u;

constexpr uint32_t kP0Accepted = kP0Clocksource | kP0AlarmWakeEn | kP0AlarmEn | kP0LckSecs |
                                 kP0Xtal24Pwrup | kP0Xtal32Pwrup | kP0Xtal32Freq | kP0AlarmWake |
                                 kP0MsecRes | kP0Rsvd1 | kP0AutoRestart;

/* MCIMX28RM Rev. 2 sections 22.2 and 22.8.7. */
inline const char* Imx28RtcP0Reject(uint32_t old_value, uint32_t next, bool xtal32768_present,
                                    bool xtal32000_present) {
    const bool crystal   = (next & kP0Clocksource) != 0u;
    const bool freq32000 = (next & kP0Xtal32Freq) != 0u;
    if (((old_value ^ next) & ~kP0Accepted) != 0u) return "bits outside the accepted set changed";
    if (crystal && !freq32000 && !xtal32768_present) {
        return "CLOCKSOURCE 1, XTAL32_FREQ 0, XTAL32768_PRESENT 0";
    }
    if (crystal && freq32000 && !xtal32000_present) {
        return "CLOCKSOURCE 1, XTAL32_FREQ 1, XTAL32000_PRESENT 0";
    }
    if (crystal && (next & kP0Xtal32Pwrup) == 0u) return "CLOCKSOURCE 1, XTAL32KHZ_PWRUP 0";
    return nullptr;
}

}
