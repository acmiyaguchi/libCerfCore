#pragma once

#include <cstdint>
#include <ctime>

namespace host_platform {

int64_t MonotonicTicks();
int64_t MonotonicFrequency();
void    CpuRelax();
void    LocalCalendarTime(std::time_t t, std::tm& out);

}
