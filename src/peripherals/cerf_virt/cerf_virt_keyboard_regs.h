#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kKbWriteSeq  = 0x00u;
const uint32_t kKbRingBase  = 0x10u;
const uint32_t kKbRingCount = 256u;

const uint32_t kKbEntryVkMask   = 0x00FFu;
const uint32_t kKbEntryKeyUpBit = 0x0100u;

}
