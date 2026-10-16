#include "host_process_memory.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

ProcessMemoryUsage QueryProcessMemory() { NoHostImplementation("QueryProcessMemory"); }
ThreadStackUsage   MeasureThreadStacks() { NoHostImplementation("MeasureThreadStacks"); }

}

}
