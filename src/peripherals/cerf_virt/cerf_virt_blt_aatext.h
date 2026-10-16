#pragma once

#include <cerfcore/peripherals/cerf_virt/cerf_virt_blt_descriptor.h>
#include "cerf_virt_blt_pixelops.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace CerfVirt {

/* jornada720 ddi.dll sub_FFC7D8 (table 0x1007970); ipaq_h3600_ppc2000 ddi.dll sub_F62650 */
inline constexpr uint16_t kClearTypeSampleCountRgb565[kCerfClearTypeSampleCountIndices] = {
    0xFFFF, 0xFFF6, 0xCF29, 0xFF7F, 0xFF76, 0xD6A8, 0xFEFF, 0xFEF6, 0xD628, 0xFE5F,
    0xFE56, 0xD5A7, 0xFDDF, 0xFDD6, 0xDD46, 0xFD5F, 0xFD56, 0xDCC6, 0xE55F, 0xE556,
    0xC4E5, 0xDCFF, 0xDCF6, 0xC485, 0xF4FA, 0xD496, 0xD480, 0xE276, 0xD22F, 0xB180,
    0xE7FF, 0xE7F6, 0xAF29, 0xE77F, 0xE776, 0xB6A8, 0xE6FF, 0xE6F6, 0xB628, 0xE65F,
    0xE656, 0xBDA7, 0xE5DF, 0xE5D6, 0xBD46, 0xE55F, 0xE556, 0xBCC6, 0xE4BF, 0xE4B6,
    0xC445, 0xE43F, 0xE436, 0xC3C5, 0xDCBA, 0xC456, 0xC440, 0xD236, 0xC1EF, 0x9960,
    0xC7FF, 0xC7F6, 0x8F29, 0xC77F, 0xC776, 0x96A8, 0xC6FF, 0xC6F6, 0x9628, 0xC65F,
    0xC656, 0x9DA7, 0xC5DF, 0xC5D6, 0x9D46, 0xC55F, 0xC556, 0xA4C6, 0xC4BF, 0xC4B6,
    0xA445, 0xC43F, 0xC436, 0xA3C5, 0xDC1A, 0xC3B6, 0xC3A0, 0xB9F6, 0xA9AF, 0x8920,
    0xA7FF, 0xA7F6, 0x6F29, 0xA77F, 0xA776, 0x76A8, 0xA6FF, 0xA6F6, 0x7628, 0xA65F,
    0xA656, 0x7DA7, 0xA5DF, 0xA5D6, 0x7D46, 0xA55F, 0xA556, 0x84C6, 0xA4BF, 0xA4B6,
    0x8445, 0xA43F, 0xA436, 0x8BC5, 0xBC1A, 0xA3B6, 0xA3A0, 0xA1D6, 0x918F, 0x7100,
    0xAF3F, 0xAF36, 0x7669, 0xA6DF, 0xA6D6, 0x7608, 0x86FF, 0x86F6, 0x5628, 0x865F,
    0x8656, 0x5DA7, 0x85DF, 0x85D6, 0x5D46, 0x855F, 0x8556, 0x64C6, 0x84BF, 0x84B6,
    0x6445, 0x843F, 0x8436, 0x6BC5, 0x9C1A, 0x83B6, 0x83A0, 0x8996, 0x794F, 0x58C0,
    0x4E7F, 0x4E76, 0x1D89, 0x45FF, 0x45F6, 0x1D28, 0x459F, 0x4596, 0x14C8, 0x3D1F,
    0x3D16, 0x1467, 0x34BF, 0x34B6, 0x1406, 0x343F, 0x3436, 0x13A6, 0x2BDF, 0x2BD6,
    0x0B45, 0x2B7F, 0x2B76, 0x0AE5, 0x3B5A, 0x22F6, 0x22E0, 0x30D6, 0x208F, 0x0000,
};

struct ClearTypeTables {
    std::array<std::array<uint8_t, 3>, 115> coverage{};

    ClearTypeTables() {
        std::size_t index = 0u;
        for (int r = 0; r <= 6; ++r) {
            const int first_g = r > 2 ? r - 2 : 0;
            const int last_g = r < 4 ? r + 2 : 6;
            for (int g = first_g; g <= last_g; ++g) {
                const int first_b = std::max(0, std::max(g - 2, g - r));
                const int last_b = std::min(6, std::min(g + 2, g + 6 - r));
                for (int b = first_b; b <= last_b; ++b) {
                    coverage[index++] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
                }
            }
        }
    }

    static const ClearTypeTables& Get() {
        static const ClearTypeTables tables;
        return tables;
    }
};

struct AATextContext {
    uint32_t fl[3];
    int      shR[3];
    int      shL[3];
    uint32_t uF[3];
    uint32_t aulB[16];
    uint32_t aulIB[16];
    const ClearTypeTables* ct = nullptr;
    const uint16_t* ct_sample = nullptr;
    bool     ct_sample_fg_white = false;
    uint32_t s565_mask[3];
    uint32_t s565_shift[3];
    uint32_t s565_bits[3];
    uint32_t d_shift[3];

    void Build(const uint32_t masks[3], uint32_t on_color, float gamma = 2.330f) {
        for (int c = 0; c < 3; ++c) {
            fl[c] = masks[c];
            int r = (int)BltPixelOps::HighBitPos(masks[c]) - 8;
            int l = 0;
            if (r < 0) { l = -r; r = 0; }
            shR[c] = r;
            shL[c] = l;
            uF[c] = ((on_color & masks[c]) >> r) << l;
        }
        for (int k = 0; k < 16; ++k) {
            const float a = (k > 0) ? (float)(k + 1) : 0.0f;
            aulB[k]  = (uint32_t)(65536.0f * std::pow(a / 16.0f, 1.0f / gamma));
            aulIB[k] = (uint32_t)(65536.0f - 65536.0f * std::pow(1.0f - a / 16.0f, 1.0f / gamma));
        }
    }

    void UseClearType() { ct = &ClearTypeTables::Get(); }

    void UseClearTypeSampleCount(uint32_t on_color) {
        ct_sample = kClearTypeSampleCountRgb565;
        uint32_t s565_bpp = 0;
        BltPixelOps::FormatMasks(kCerfFmt16Bpp, s565_mask, &s565_bpp);
        for (int c = 0; c < 3; ++c) {
            s565_shift[c] = 32u - BltPixelOps::HighBitPos(s565_mask[c]);
            s565_bits[c]  = BltPixelOps::PopCount(s565_mask[c]);
            d_shift[c]    = 32u - BltPixelOps::HighBitPos(fl[c]);
        }
        const uint32_t rgb = fl[0] | fl[1] | fl[2];
        ct_sample_fg_white = (on_color & rgb) == rgb;
    }

    uint32_t BlendAA(uint32_t dst, uint32_t cov) const {
        uint32_t u = 0;
        for (int c = 0; c < 3; ++c) {
            const uint32_t uT = ((dst & fl[c]) << shL[c]) >> shR[c];
            const uint32_t dT = uF[c] - uT;
            const uint32_t* tab = ((int32_t)dT < 0) ? aulIB : aulB;
            u |= ((((dT * tab[cov] + (uT << 16)) >> 16) << shR[c]) >> shL[c]) & fl[c];
        }
        return u;
    }

    uint32_t BlendClearTypeSampleCount(uint32_t dst, uint32_t mask_index) const {
        const uint32_t last = kCerfClearTypeSampleCountIndices - 1u;
        const uint32_t t = BltPixelOps::ConvertPixel(
            ct_sample[ct_sample_fg_white ? last - mask_index : mask_index],
            s565_mask, s565_shift, s565_bits, fl, d_shift);
        uint32_t u = 0;
        for (int c = 0; c < 3; ++c) {
            const uint32_t uT = ((dst & fl[c]) << shL[c]) >> shR[c];
            const uint32_t tT = ((t & fl[c]) << shL[c]) >> shR[c];
            const uint32_t out = std::clamp(tT, std::min(uF[c], uT), std::max(uF[c], uT));
            u |= (((out << shR[c]) >> shL[c]) & fl[c]);
        }
        return u;
    }

    uint32_t BlendClearType(uint32_t dst, uint32_t mask_index) const {
        if (ct_sample) return BlendClearTypeSampleCount(dst, mask_index);
        const auto& coverage = ct->coverage[mask_index];
        static constexpr int32_t kBlend[7] = {0x000000, 0x02AAAB, 0x055555, 0x080000, 0x0AAAAB, 0x0D5555, 0x100000};
        uint32_t u = 0;
        for (int c = 0; c < 3; ++c) {
            const uint32_t uT = ((dst & fl[c]) << shL[c]) >> shR[c];
            const int32_t bg = static_cast<int32_t>(uT);
            const int32_t fg = static_cast<int32_t>(uF[c]);
            const uint32_t out = static_cast<uint32_t>(bg + (((fg - bg) * kBlend[coverage[c]] + 0x80000) >> 20));
            u |= (((out << shR[c]) >> shL[c]) & fl[c]);
        }
        return u;
    }
};

}
