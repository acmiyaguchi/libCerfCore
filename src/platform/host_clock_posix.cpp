#include "host_clock.h"

#include "host_unimplemented.h"

namespace host_platform {

int64_t MonotonicTicks() { NoHostImplementation("MonotonicTicks"); }
int64_t MonotonicFrequency() { NoHostImplementation("MonotonicFrequency"); }
void    CpuRelax() { NoHostImplementation("CpuRelax"); }
void    LocalCalendarTime(std::time_t, std::tm&) { NoHostImplementation("LocalCalendarTime"); }

}
