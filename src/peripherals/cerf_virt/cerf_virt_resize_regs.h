#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kRszWantW   = 0x00u;
const uint32_t kRszWantH   = 0x04u;
const uint32_t kRszWantBpp = 0x08u;
const uint32_t kRszWantGen = 0x0Cu;

const uint32_t kRszAppliedW   = 0x10u;
const uint32_t kRszAppliedH   = 0x14u;
const uint32_t kRszAppliedGen = 0x18u;

}
