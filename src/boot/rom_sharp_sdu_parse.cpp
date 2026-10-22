#include "rom_sharp_sdu_parse.h"

#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace cerf::rom_image_parse {

namespace {

using cerf::le::U32;

uint32_t ItemSize(std::span<const uint8_t> raw, uint32_t item) {
    return U32(raw.data(), kSharpSduItemTableOff + size_t(item) * kSharpSduItemEntrySize);
}

}  /* namespace */

bool SharpSduHeaderValid(std::span<const uint8_t> raw) {
    if (raw.size() < kSharpSduHeaderBytes) return false;
    if (U32(raw.data(), 0) != kSharpSduHeaderBytes) return false;
    const uint64_t file_size = uint64_t(U32(raw.data(), kSharpSduFileSizeOff))
                             | uint64_t(U32(raw.data(), kSharpSduFileSizeOff + 4)) << 32;
    if (file_size != raw.size()) return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < kSharpSduHeaderBytes; ++i) sum = (sum + raw[i]) & 0xFFFFu;
    return (sum | ((0xFFFFu - sum) << 16)) == U32(raw.data(), kSharpSduChecksumOff);
}

bool SharpSduLocateOsXip(std::span<const uint8_t> raw, SharpSduOsXip& out) {
    if (!SharpSduHeaderValid(raw)) return false;
    if (U32(raw.data(), kSharpSduItemCountOff) <= kSharpSduOsItem) return false;

    uint64_t item_off = kSharpSduHeaderBytes;
    for (uint32_t k = 0; k < kSharpSduOsItem; ++k) item_off += ItemSize(raw, k);
    const uint32_t item_size = ItemSize(raw, kSharpSduOsItem);
    if (item_size <= kSharpSduBootStreamBytes) return false;
    if (item_off + item_size > raw.size()) return false;

    const size_t nk_off  = size_t(item_off) + kSharpSduBootStreamBytes;
    const size_t nk_span = item_size - kSharpSduBootStreamBytes;
    std::span<const uint8_t> nk = raw.subspan(nk_off, nk_span);

    EcecRecord ecec;
    if (!ReadEcec(nk, kRomSignatureOffset, ecec)) return false;

    ParsedROMHDR h;
    uint32_t     base = 0;
    if (!FindSelfLocatingRomhdr(
            nk, ecec.ptoc, 0x1000u, 0u,
            [&](const ParsedROMHDR& c) { return c.physlast - c.physfirst <= nk_span; },
            h, base)) {
        return false;
    }
    out.data_off  = nk_off;
    out.flat_size = h.physlast - h.physfirst;
    out.base_va   = base;
    return true;
}

}  /* namespace cerf::rom_image_parse */

}
