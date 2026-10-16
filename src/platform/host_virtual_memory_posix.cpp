#include "host_virtual_memory.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

uint8_t* AllocateZeroed(size_t) { NoHostImplementation("AllocateZeroed"); }
void     Release(uint8_t*) { NoHostImplementation("Release"); }

}

}
