#pragma once

#include <cstdint>

namespace host_platform {

struct ProcessMemoryUsage {
    uint64_t private_bytes = 0;
    uint64_t working_set   = 0;
};

struct ThreadStackUsage {
    uint64_t count     = 0;
    uint64_t committed = 0;
    uint64_t reserved  = 0;
};

ProcessMemoryUsage QueryProcessMemory();
ThreadStackUsage   MeasureThreadStacks();

}
