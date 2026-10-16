#include "host_clock.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace host_platform {

int64_t MonotonicTicks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

int64_t MonotonicFrequency() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart;
}

void CpuRelax() {
    YieldProcessor();
}

void LocalCalendarTime(std::time_t t, std::tm& out) {
    localtime_s(&out, &t);
}

}
