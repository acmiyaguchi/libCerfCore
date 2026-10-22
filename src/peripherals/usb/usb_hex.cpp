#include "usb_hex.h"

#include <cstdio>

namespace cerfcore {

std::string UsbHex(const uint8_t* bytes, size_t n) {
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02X ", bytes[i]);
        out += b;
    }
    return out;
}

}
