#include "host_thread.h"

#include "host_unimplemented.h"

namespace host_platform {

uint32_t CurrentThreadId() { NoHostImplementation("CurrentThreadId"); }

}
