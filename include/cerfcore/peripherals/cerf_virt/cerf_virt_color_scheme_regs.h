#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kCscMagic   = 0xC0105CEEu;

const uint32_t kCscPresent = 0x00u;
const uint32_t kCscCount   = 0x04u;
const uint32_t kCscEntries = 0x10u;

}
