#pragma once

#include <cerfcore/core/log.h>

#include <cstdio>
#include <cstdlib>

namespace cerfcore {

namespace host_platform {

[[noreturn]] inline void NoHostImplementation(const char* what) {
    std::fprintf(stderr, "host_platform::%s: no implementation on this host\n", what);
    std::fflush(stderr);
    std::_Exit(CERF_FATAL_RUNTIME_ERROR);
}

}

}
