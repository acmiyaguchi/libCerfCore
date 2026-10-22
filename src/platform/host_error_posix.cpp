#include "host_error.h"

#include <cerrno>
#include <cstring>

namespace cerfcore {

namespace host_platform {

namespace {

[[maybe_unused]] const char* StrerrorText(int, const char* buf) { return buf; }
[[maybe_unused]] const char* StrerrorText(const char* text, const char*) { return text; }

}

uint32_t LastErrorCode() { return static_cast<uint32_t>(errno); }

std::string ErrorText(uint32_t code) {
    char        buf[256];
    const char* text = StrerrorText(::strerror_r(static_cast<int>(code), buf, sizeof(buf)), buf);
    return std::string(text) + " (errno " + std::to_string(code) + ")";
}

}

}
