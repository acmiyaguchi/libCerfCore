#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cerfengine/byte_order.h>

namespace cerf {

using cerfengine::ByteWidthMask;

constexpr uint16_t ByteSwap16(uint16_t v) { return uint16_t((v >> 8) | (v << 8)); }

constexpr uint32_t ByteSwap32(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

}

namespace cerf::le {

using cerfengine::le::Put16;
using cerfengine::le::Put24;
using cerfengine::le::Put32;
using cerfengine::le::Put64;
using cerfengine::le::PutN;
using cerfengine::le::U16;
using cerfengine::le::U24;
using cerfengine::le::U32;
using cerfengine::le::U64;
using cerfengine::le::UN;

inline void Append16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}

inline void Append32(std::vector<uint8_t>& out, uint32_t v) {
    Append16(out, uint16_t(v));
    Append16(out, uint16_t(v >> 16));
}

inline void AppendN(std::vector<uint8_t>& out, uint64_t v, size_t width) {
    const size_t at = out.size();
    out.resize(at + width);
    PutN(out.data() + at, v, width);
}

}

namespace cerf::be {

inline uint16_t U16(const uint8_t* p, size_t off = 0) {
    return uint16_t((p[off] << 8) | p[off + 1]);
}

inline uint32_t U32(const uint8_t* p, size_t off = 0) {
    return (uint32_t(U16(p, off)) << 16) | U16(p, off + 2);
}

inline void Put16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}

inline void Append16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

inline void Append32(std::vector<uint8_t>& out, uint32_t v) {
    Append16(out, uint16_t(v >> 16));
    Append16(out, uint16_t(v));
}

}
