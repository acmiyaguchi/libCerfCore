#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cerfcore {

std::string UsbHex(const uint8_t* bytes, size_t n);

}
