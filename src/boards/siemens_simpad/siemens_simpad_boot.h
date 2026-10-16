#pragma once

#include <cstdint>

namespace cerfcore {

/* The SIMpad SL4 boot stub copies the image head from flash to DRAM before
   enabling the MMU (nk.exe 0x80081010-0x80081018:
   LDR R2,=0x4080000; LDR R3,=0xC1E80000; MOV R4,#0x180000; word copy loop).
   The copy source has no OAT VA mapping, so the board backs and fills it. */
namespace siemens_simpad {
constexpr uint32_t kHeadCopySrcPa = 0x04080000u;
constexpr uint32_t kHeadLen       = 0x00180000u;
}  /* namespace simpad_sl4 */

}
