#pragma once

#include <cstdint>

namespace cerf_msm8255_sdcc_detail {

constexpr uint32_t kSdc1Base       = 0xA0400000u;
constexpr uint32_t kSdc1Size       = 0x00000800u;
constexpr uint32_t kSdc1ResetClock = 130u;
constexpr uint32_t kSdc1SlotIndex  = 0u;
constexpr uint32_t kSdc1IrqSource0 = 94u;
constexpr uint32_t kSdc1IrqSource1 = 95u;
constexpr uint32_t kSdc1Crci       = 6u;

constexpr uint32_t kSdc2Base       = 0xA0500000u;
constexpr uint32_t kSdc2Size       = 0x00000800u;
constexpr uint32_t kSdc2ResetClock = 131u;
constexpr uint32_t kSdc2SlotIndex  = 1u;
constexpr uint32_t kSdc2IrqSource0 = 98u;
constexpr uint32_t kSdc2IrqSource1 = 99u;
constexpr uint32_t kSdc2Crci       = 7u;

constexpr uint32_t kSdc3Base       = 0xA3000000u;
constexpr uint32_t kSdc3Size       = 0x00000800u;
constexpr uint32_t kSdc3ResetClock = 132u;
constexpr uint32_t kSdc3SlotIndex  = 2u;
constexpr uint32_t kSdc3IrqSource0 = 96u;
constexpr uint32_t kSdc3IrqSource1 = 97u;
constexpr uint32_t kSdc3Crci       = 12u;

constexpr uint32_t kSdc4Base       = 0xA3100000u;
constexpr uint32_t kSdc4Size       = 0x00000800u;
constexpr uint32_t kSdc4ResetClock = 133u;
constexpr uint32_t kSdc4SlotIndex  = 3u;
constexpr uint32_t kSdc4IrqSource0 = 100u;
constexpr uint32_t kSdc4IrqSource1 = 101u;
constexpr uint32_t kSdc4Crci       = 13u;

}  // namespace cerf_msm8255_sdcc_detail
