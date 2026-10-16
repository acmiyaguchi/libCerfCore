#include "host_thread.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace host_platform {

uint32_t CurrentThreadId() { return GetCurrentThreadId(); }

}
