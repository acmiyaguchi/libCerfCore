#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
#else
#include <cstdint>
#endif

namespace CerfVirt {

const uint32_t kCurKick   = 0x004u;

const uint32_t kCursorMaxDim    = 64u;
const uint32_t kCursorMaxStride = (kCursorMaxDim + 7u) / 8u;
const uint32_t kCursorBitsBytes = kCursorMaxStride * kCursorMaxDim * 2u;

struct CerfCursorDescriptor {
    uint32_t visible;
    uint32_t cx;
    uint32_t cy;
    uint32_t xhot;
    uint32_t yhot;
    uint32_t stride;
    uint8_t  bits[kCursorBitsBytes];
};

}
