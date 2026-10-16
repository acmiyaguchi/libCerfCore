#include "host_thread.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

uint32_t CurrentThreadId() { NoHostImplementation("CurrentThreadId"); }

}

}
