#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kCalSigEvent = 0x00u;

const uint32_t kCalSigAppeared    = 1u;
const uint32_t kCalSigDisappeared = 2u;

}
