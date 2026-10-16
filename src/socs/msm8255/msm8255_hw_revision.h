#pragma once

#include <cstdint>

namespace cerf_msm8255_hw_revision {

constexpr uint32_t kNumber = 0x270u;

/* OpenOCD src/jtag/core.c EXTRACT_VER, EXTRACT_PART and EXTRACT_MFG split an
   IEEE 1149.1 IDCODE into bits 31-28, 27-12 and 11-1. */
constexpr uint32_t kRevisionShift     = 28u;
constexpr uint32_t kPartNumShift      = 12u;
constexpr uint32_t kManufacturerShift = 1u;

/* OpenOCD src/jtag/core.c reads a zero in IDCODE bit 0 as a device sitting in
   bypass, and reports that the tap has no IDCODE. */
constexpr uint32_t kValidBit = 1u;

constexpr uint32_t kRevision = 1u;

constexpr uint32_t kPartNum = 0x570u;

/* OpenOCD src/helper/jep106.inc places Qualcomm at bank 0 id 0x70, which
   src/helper/jep106.h composes as (bank << 7) | id. */
constexpr uint32_t kManufacturer = 0x070u;

constexpr uint32_t Word() {
    return (kRevision << kRevisionShift) | (kPartNum << kPartNumShift) |
           (kManufacturer << kManufacturerShift) | kValidBit;
}

constexpr bool Read(uint32_t off, uint32_t& value) {
    if (off != kNumber) return false;
    value = Word();
    return true;
}

}  // namespace cerf_msm8255_hw_revision
