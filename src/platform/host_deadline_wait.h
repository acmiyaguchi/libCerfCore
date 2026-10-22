#pragma once

#include <cerrno>
#include <cstdint>
#include <time.h>

namespace cerfcore {

namespace host_platform {

template <class WaitReadable>
bool SleepUnlessReadable(int64_t ns, WaitReadable wait) {
    timespec deadline{};
    ::clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += static_cast<time_t>(ns / 1000000000);
    deadline.tv_nsec += static_cast<long>(ns % 1000000000);
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000;
    }
    for (;;) {
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        timespec left{deadline.tv_sec - now.tv_sec, deadline.tv_nsec - now.tv_nsec};
        if (left.tv_nsec < 0) {
            left.tv_sec -= 1;
            left.tv_nsec += 1000000000;
        }
        if (left.tv_sec < 0) return true;
        const int ready = wait(left);
        if (ready == 0) return true;
        if (ready > 0) return false;
        if (errno != EINTR) return false;
    }
}

}

}
