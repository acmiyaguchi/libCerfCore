#include "host_thread.h"

#include <unistd.h>

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

uint32_t CurrentThreadId() {
#if defined(__linux__)
    return static_cast<uint32_t>(::gettid());
#else
    NoHostImplementation("CurrentThreadId");
#endif
}

}

}
