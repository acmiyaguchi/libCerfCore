#pragma once

#include <cstdint>

namespace cerfcore {

namespace host_platform {

int64_t MonotonicTicks();
int64_t MonotonicFrequency();
void    CpuRelax();

}

}
