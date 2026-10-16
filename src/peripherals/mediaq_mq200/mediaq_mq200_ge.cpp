#include "mediaq_mq200_ge.h"

#include "mediaq_mq200_ge_pixel.h"
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace {

constexpr uint32_t kCmdSrcStrideSelect  = 1u << 24;
constexpr uint32_t kCmdAutoExecute      = 1u << 27;
constexpr uint32_t kGe01NoLastPixel     = 1u << 30;
constexpr uint32_t kBitBltGe01Reserved  = 0x7000F000u;
constexpr uint32_t kBitBltGe02Reserved  = 0x10001000u;
constexpr uint32_t kSourceLineStride    = 0xFFFu;
constexpr uint64_t kLinedRowAlignBytes  = 16u;

/* MQ-200 Data Book Table 5-72: GE09R packed fields [2:0] bit, [5:3] byte, [31:25] mono bit
   space, [31:28] colour byte space (bit 31 zero on a 32-bit CPU interface); lined [30:28]. */
uint32_t MonoBitOffset(uint32_t g09)                { return ((g09 >> 3) & 7u) * 8u + (g09 & 7u); }
uint32_t MonoPitchBits(uint32_t g09, uint32_t w)    { return w + ((g09 >> 25) & 0x7Fu); }
uint32_t PackedColorOffset(uint32_t g09)            { return (g09 >> 3) & 7u; }
uint32_t PackedColorSpace(uint32_t g09)             { return (g09 >> 28) & 7u; }
uint32_t LinedLeadBytes(uint32_t g09)               { return (g09 >> 28) & 7u; }

uint64_t LinedRowBytes(uint32_t g09, uint32_t w, uint32_t bpp) {
    const uint64_t used = LinedLeadBytes(g09) + static_cast<uint64_t>(w) * bpp;
    return (used + kLinedRowAlignBytes - 1u) / kLinedRowAlignBytes * kLinedRowAlignBytes;
}

}

const MediaQGe::Layout& MediaQMq200Ge::Lyt() const {
    static const Layout kL{0x07u, 0x08u, 0x42u, 0x09u, 0xFFFu, 0x1FFFF8u,
                           0x43u, 0x40u, 0x41u, 0x40u};
    return kL;
}

bool MediaQMq200Ge::AutoExecute() const {
    return (reg_[kGe00Command] & kCmdAutoExecute) != 0u;
}

uint32_t MediaQMq200Ge::ExpectedSourceDwords() const {
    const uint32_t w = reg_[kGe01Size] & 0xFFFu;
    const uint32_t h = (reg_[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return 0u;
    const uint32_t g09 = reg_[kGe09SrcOff];
    if (reg_[kGe00Command] & kCmdMonoSrc) {
        const uint64_t bits = MonoBitOffset(g09) +
                              static_cast<uint64_t>(h - 1u) * MonoPitchBits(g09, w) + w;
        return static_cast<uint32_t>((bits + 31u) / 32u);
    }
    const uint32_t bpp = BytesPerPixel();
    if (reg_[kGe00Command] & kCmdPacked) {
        const uint64_t row   = static_cast<uint64_t>(w) * bpp;
        const uint64_t bytes = PackedColorOffset(g09) +
                               static_cast<uint64_t>(h - 1u) * (row + PackedColorSpace(g09)) + row;
        return static_cast<uint32_t>((bytes + 3u) / 4u);
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(h) * LinedRowBytes(g09, w, bpp) / 4u);
}

void MediaQMq200Ge::BlitScreen(const uint32_t* r) {
    Blit(r, false);
}

void MediaQMq200Ge::BlitSystem(const uint32_t* r) {
    Blit(r, true);
}

uint32_t MediaQMq200Ge::MonoSourceBit(const uint32_t* r, uint32_t col, uint32_t row,
                                      bool& present) const {
    const uint32_t g09 = r[kGe09SrcOff];
    const uint64_t bit = MonoBitOffset(g09) +
                         static_cast<uint64_t>(row) * MonoPitchBits(g09, r[kGe01Size] & 0xFFFu) + col;
    present = bit < static_cast<uint64_t>(src_fifo_.size()) * 32u;
    return present ? MonoBit(SrcFifoBytes(), bit) : 0u;
}

uint32_t MediaQMq200Ge::ColorSourcePixel(const uint32_t* r, uint32_t col, uint32_t row,
                                         bool& present) const {
    const uint32_t g09 = r[kGe09SrcOff];
    const uint32_t w   = r[kGe01Size] & 0xFFFu;
    const uint32_t bpp = DepthBytes(r[kGe0ADstStride]);
    const uint64_t pos = (r[kGe00Command] & kCmdPacked)
        ? PackedColorOffset(g09) +
              static_cast<uint64_t>(row) * (static_cast<uint64_t>(w) * bpp + PackedColorSpace(g09)) +
              static_cast<uint64_t>(col) * bpp
        : static_cast<uint64_t>(row) * LinedRowBytes(g09, w, bpp) + LinedLeadBytes(g09) +
              static_cast<uint64_t>(col) * bpp;
    present = pos + bpp <= static_cast<uint64_t>(src_fifo_.size()) * 4u;
    return present ? static_cast<uint32_t>(cerf::le::UN(SrcFifoBytes() + pos, bpp)) : 0u;
}

void MediaQMq200Ge::Blit(const uint32_t* r, bool system_source) {
    const uint32_t cmd = r[kGe00Command];
    const uint32_t w = r[kGe01Size] & 0xFFFu;
    const uint32_t h = (r[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return;
    Pixel px(r, Lyt(), Fb(), FbBytes());

    const int dx   = static_cast<int>(r[kGe02DstXY] & 0xFFFu);
    const int dy   = static_cast<int>((r[kGe02DstXY] >> 16) & 0xFFFu);
    const int xdir = (cmd & kCmdXNeg) ? -1 : 1;
    const int ydir = (cmd & kCmdYNeg) ? -1 : 1;

    const bool     mono_src  = (cmd & kCmdMonoSrc) != 0u;
    const bool     mono_pat  = (cmd & kCmdMonoPat) != 0u;
    const bool     solid     = (cmd & kCmdSolidSrc) != 0u;
    const uint32_t trans_bit = (cmd & kCmdMonoTrPol) ? 1u : 0u;
    const bool     src_trans = (cmd & kCmdMonoTrans) && mono_src;
    const bool     pat_trans = (cmd & kCmdMonoTrans) && mono_pat && !mono_src;
    const bool     need_s    = px.ReadsSource() || src_trans;
    const bool     need_p    = px.ReadsPattern() || pat_trans;
    const uint32_t fg        = r[kGe07FgColor] & px.Mask();
    const uint32_t bg        = r[kGe08BgColor] & px.Mask();

    const int64_t src_line = (cmd & kCmdSrcStrideSelect) ? (r[kGe09SrcOff] & kSourceLineStride)
                                                         : px.Stride();
    const int64_t src_first = static_cast<int64_t>(px.Base()) +
        static_cast<int64_t>((r[kGe03SrcXY] >> 16) & 0xFFFu) * px.Stride() +
        static_cast<int64_t>(r[kGe03SrcXY] & 0xFFFu) * px.Bpp();

    for (uint32_t row = 0; row < h; ++row) {
        const int y = dy + ydir * static_cast<int>(row);
        for (uint32_t col = 0; col < w; ++col) {
            const int x = dx + xdir * static_cast<int>(col);
            if (px.Outside(x, y)) continue;
            uint32_t s = 0u;
            if (need_s) {
                bool present = true;
                if (mono_src) {
                    const uint32_t bit = solid ? 1u : MonoSourceBit(r, col, row, present);
                    if (!present || (src_trans && bit == trans_bit)) continue;
                    s = bit ? fg : bg;
                } else if (system_source) {
                    s = ColorSourcePixel(r, col, row, present);
                    if (!present) continue;
                } else if (!px.Fetch(src_first + ydir * static_cast<int64_t>(row) * src_line +
                                     xdir * static_cast<int64_t>(col) * px.Bpp(), s)) {
                    continue;
                }
            }
            uint32_t p = 0u;
            if (need_p) {
                const uint32_t lx = static_cast<uint32_t>(x - dx);
                const uint32_t ly = static_cast<uint32_t>(y - dy);
                if (mono_pat) {
                    const uint32_t bit = solid ? 1u : PatternBit(r, lx, ly);
                    if (pat_trans && bit == trans_bit) continue;
                    p = PatternColor(r, bit);
                } else {
                    p = PatternOperand(r, lx, ly);
                }
            }
            px.Put(x, y, p, s);
        }
    }
}

void MediaQMq200Ge::DrawLine(uint32_t cmd) {
    Pixel px(reg_, Lyt(), Fb(), FbBytes());
    const bool mono_src = (cmd & kCmdMonoSrc) != 0u;
    const bool mono_pat = (cmd & kCmdMonoPat) != 0u;
    const bool solid    = (cmd & kCmdSolidSrc) != 0u;
    const bool mono_trans_fg = (cmd & kCmdMonoTrans) && (cmd & kCmdMonoTrPol) && (mono_src || mono_pat);
    if (mono_trans_fg) return;
    const uint32_t s = (mono_src && solid) ? (reg_[kGe07FgColor] & px.Mask()) : 0u;
    const uint32_t p = (mono_pat && solid) ? PatternColor(reg_, 1u) : 0u;

    const uint32_t ge01 = reg_[kGe01Size];    /* LINE_DRAW   */
    const uint32_t ge02 = reg_[kGe02DstXY];   /* LINE_MAJOR_X */
    const uint32_t ge03 = reg_[kGe03SrcXY];   /* LINE_MINOR_Y */

    auto s17 = [](uint32_t v) -> long {
        v &= 0x1FFFFu;
        return (v & 0x10000u) ? static_cast<long>(v) - 0x20000 : static_cast<long>(v);
    };

    int x = static_cast<int>(ge02 & 0xFFFu);          /* start X */
    int y = static_cast<int>(ge03 & 0xFFFu);          /* start Y */
    const uint32_t cPels = (ge01 >> 17) & 0xFFFu;     /* major-axis pixel count */
    const bool y_major   = (ge01 & 0x20000000u) != 0u;/* GE01R[29] = Y_MAJOR */
    const long gamma = s17(ge01 & 0x1FFFFu);          /* GE01R[16:0] error term */
    const long dM = s17((ge02 >> 12) & 0x1FFFFu);     /* GE02R[28:12] major delta */
    const long dN = s17((ge03 >> 12) & 0x1FFFFu);     /* GE03R[28:12] minor delta */
    const int step_x = (cmd & 0x800u)  ? -1 : 1;      /* GE00R X_DIR */
    const int step_y = (cmd & 0x1000u) ? -1 : 1;      /* GE00R Y_DIR */
    const uint32_t count = cPels - ((ge01 & kGe01NoLastPixel) && cPels ? 1u : 0u);

    long accum = dN + gamma;
    const long axstp = dN;
    const long dgstp = dN - dM;
    for (uint32_t i = 0; i < count; ++i) {
        if (!px.Outside(x, y)) px.Put(x, y, p, s);
        if (i + 1u >= count) break;
        if (y_major) y += step_y; else x += step_x;   /* major-axis step */
        if (axstp != 0) {
            if (accum < 0) accum += axstp;
            else { if (y_major) x += step_x; else y += step_y; accum += dgstp; }
        }
    }
}

const char* MediaQMq200Ge::CommandUnmodeled(uint32_t cmd) const {
    const CommandPath path = PathOf(cmd);
    if (path == CommandPath::Nop || path == CommandPath::Unmodeled) return nullptr;
    const uint8_t rop      = Pixel::Rop(cmd);
    const bool    mono_src = (cmd & kCmdMonoSrc) != 0u;
    const bool    mono_pat = (cmd & kCmdMonoPat) != 0u;
    const bool    solid    = (cmd & kCmdSolidSrc) != 0u;
    const bool    need_s   = Pixel::ReadsSource(rop) || ((cmd & kCmdMonoTrans) && mono_src);
    const bool    need_p   = Pixel::ReadsPattern(rop) || ((cmd & kCmdMonoTrans) && mono_pat && !mono_src);
    if (path == CommandPath::Line) {
        if (need_s && !(mono_src && solid)) return "draws a line from a source other than the solid colour";
        if (need_p && !(mono_pat && solid)) return "draws a line from a pattern other than the solid colour";
        return nullptr;
    }
    if ((reg_[kGe01Size] & kBitBltGe01Reserved) || (reg_[kGe02DstXY] & kBitBltGe02Reserved)) {
        return "starts a BitBLT with GE01R / GE02R reserved bits set";
    }
    if (mono_src && (cmd & kCmdXNeg)) return "sets the negative X direction with a mono source";
    if (path == CommandPath::ScreenMono) {
        return (need_s && !solid) ? "reads a mono source from display memory" : nullptr;
    }
    if (path == CommandPath::ScreenColor) {
        return (need_s && (cmd & kCmdPacked)) ? "reads a packed colour source from display memory" : nullptr;
    }
    if (cmd & (kCmdXNeg | kCmdYNeg)) return "streams a system memory source in a negative direction";
    if (cmd & kCmdSrcStrideSelect) return "selects a source line stride for a system memory source";
    if (path == CommandPath::SystemMono && !(cmd & kCmdPacked)) return "streams a lined mono source";
    if (path == CommandPath::SystemMono && solid) return "streams a mono source the solid colour replaces";
    return nullptr;
}

}
