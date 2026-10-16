#pragma once

#include <cstdint>

namespace vr41xx_bcu {

/* VR4102 UM 10.2.1-10.2.2 + 10.4.5, VR4111 UM 11.2.1-11.2.2 + 11.4.5, VR4121 UM 11.2.1-11.2.2
   + 11.4.5: ISAM/LCD (BCUCNTREG1 D13) = 0 and GMODE (BCUCNTREG2 D0) = 0 invert every data
   bit read or written through LCD space 0x0A000000-0x0AFFFFFF. */
constexpr uint32_t kOffCnt1     = 0x00u;
constexpr uint32_t kOffCnt2     = 0x02u;
constexpr uint16_t kCnt1IsamLcd = 0x2000u;
constexpr uint16_t kCnt2Gmode   = 0x0001u;
constexpr uint32_t kLcdSpaceBase = 0x0A000000u;
constexpr uint32_t kLcdSpaceEnd  = 0x0B000000u;

}
