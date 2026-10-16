#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int   uint32_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kGpeCmdDescVa = 0x000u;
const uint32_t kGpeCmdStatus = 0x004u;
const uint32_t kGpeCmdKick     = 0x800u;
const uint32_t kGpeCmdGradKick = 0x804u;
const uint32_t kGpeCmdLineKick = 0x808u;

const uint32_t kGpeStatusIdle  = 0u;
const uint32_t kGpeStatusBusy  = 1u;
const uint32_t kGpeStatusDone  = 2u;
const uint32_t kGpeStatusError = 0x80000000u;

}
