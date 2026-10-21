#pragma once

#include <cstdint>

namespace cerfcore {

namespace host_platform {

inline int FdOf(void* handle) { return static_cast<int>(reinterpret_cast<intptr_t>(handle)) - 1; }

inline void* HandleOf(int fd) { return reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1); }

}

}
