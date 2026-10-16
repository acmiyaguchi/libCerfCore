#include "host_process_memory.h"

#include "host_unimplemented.h"

namespace host_platform {

ProcessMemoryUsage QueryProcessMemory() { NoHostImplementation("QueryProcessMemory"); }
ThreadStackUsage   MeasureThreadStacks() { NoHostImplementation("MeasureThreadStacks"); }

}
