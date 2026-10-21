#include "host_clock.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

namespace {

constexpr int64_t kNanosecondsPerSecond = 1000000000;

}

int64_t MonotonicTicks() {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * kNanosecondsPerSecond + now.tv_nsec;
}

int64_t MonotonicFrequency() { return kNanosecondsPerSecond; }

void CpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    NoHostImplementation("CpuRelax");
#endif
}

}

}
