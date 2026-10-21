#include "host_error.h"

#include <cerrno>
#include <cstring>

namespace cerfcore {

namespace host_platform {

uint32_t LastErrorCode() { return static_cast<uint32_t>(errno); }

std::string ErrorText(uint32_t code) {
    char        buf[256];
    const char* text = ::strerror_r(static_cast<int>(code), buf, sizeof(buf));
    return std::string(text) + " (errno " + std::to_string(code) + ")";
}

}

}
