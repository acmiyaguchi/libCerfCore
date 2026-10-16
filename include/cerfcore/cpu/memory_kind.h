#pragma once

#include <cstdint>

enum class MemoryKind : uint8_t {
    kRam,
    kFlash,
    kRom,
};
