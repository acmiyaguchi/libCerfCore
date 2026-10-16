#include "emmc_register_encoding.h"

#include <cerfcore/core/byte_order.h>

#include <vector>

namespace cerfcore {

namespace {

void PutBits(uint32_t out[4], uint32_t start, uint32_t width, uint32_t value) {
    const uint32_t mask  = (width < 32u) ? ((1u << width) - 1u) : 0xFFFFFFFFu;
    const uint32_t off   = 3u - (start / 32u);
    const uint32_t shift = start & 31u;
    value &= mask;
    out[off] |= value << shift;
    if (width + shift > 32u) {
        out[off - 1u] |= value >> (32u - shift);
    }
}

// JEDEC JESD84-A43 section 10.2
uint8_t Crc7(const uint8_t* data, uint32_t length) {
    uint8_t crc = 0u;
    for (uint32_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            const uint8_t in = static_cast<uint8_t>((byte >> 7) & 1u);
            const uint8_t out = static_cast<uint8_t>((crc >> 6) & 1u);
            crc = static_cast<uint8_t>((crc << 1) & 0x7Fu);
            if (in ^ out) crc ^= 0x09u;
            byte = static_cast<uint8_t>(byte << 1);
        }
    }
    return crc;
}

// JEDEC JESD84-A43 Table 32, Table 34
void SealCrc7(uint32_t out[4]) {
    std::vector<uint8_t> bytes;
    for (uint32_t i = 0; i < 4u; ++i) cerf::be::Append32(bytes, out[i]);
    out[3] = (out[3] & 0xFFFFFF00u) |
             static_cast<uint32_t>((Crc7(bytes.data(), 15u) << 1) | 1u);
}

}  // namespace

void EncodeEmmcCid(const SdCardCid& cid, uint32_t out[4]) {
    for (uint32_t i = 0; i < 4u; ++i) out[i] = cerf::be::U32(cid.data(), i * 4u);
    SealCrc7(out);
}

void EncodeEmmcCsd(const EmmcCsdFields& csd, uint32_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0u;
    PutBits(out, 126u, 2u,  csd.csd_structure);
    PutBits(out, 122u, 4u,  csd.spec_vers);
    PutBits(out, 112u, 8u,  csd.taac);
    PutBits(out, 104u, 8u,  csd.nsac);
    PutBits(out,  96u, 8u,  csd.tran_speed);
    PutBits(out,  84u, 12u, csd.ccc);
    PutBits(out,  80u, 4u,  csd.read_bl_len);
    PutBits(out,  62u, 12u, csd.c_size);
    PutBits(out,  59u, 3u,  csd.vdd_r_curr_min);
    PutBits(out,  56u, 3u,  csd.vdd_r_curr_max);
    PutBits(out,  53u, 3u,  csd.vdd_w_curr_min);
    PutBits(out,  50u, 3u,  csd.vdd_w_curr_max);
    PutBits(out,  47u, 3u,  csd.c_size_mult);
    PutBits(out,  42u, 5u,  csd.erase_grp_size);
    PutBits(out,  37u, 5u,  csd.erase_grp_mult);
    PutBits(out,  32u, 5u,  csd.wp_grp_size);
    PutBits(out,  31u, 1u,  csd.wp_grp_enable);
    PutBits(out,  26u, 3u,  csd.r2w_factor);
    PutBits(out,  22u, 4u,  csd.write_bl_len);
    SealCrc7(out);
}

}
