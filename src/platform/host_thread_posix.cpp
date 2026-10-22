#include "host_thread.h"

#include <unistd.h>
#if defined(__APPLE__)
#include <pthread.h>
#endif

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

uint32_t CurrentThreadId() {
#if defined(__linux__)
    return static_cast<uint32_t>(::gettid());
#elif defined(__APPLE__)
    uint64_t id = 0;
    ::pthread_threadid_np(nullptr, &id);
    return static_cast<uint32_t>(id);
#else
    NoHostImplementation("CurrentThreadId");
#endif
}

}

}
