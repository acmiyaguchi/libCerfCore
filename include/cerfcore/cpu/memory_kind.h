#pragma once

#include <cstdint>

namespace cerfcore {

enum class MemoryKind : uint8_t {
    kRam,
    kFlash,
    kRom,
};

}
