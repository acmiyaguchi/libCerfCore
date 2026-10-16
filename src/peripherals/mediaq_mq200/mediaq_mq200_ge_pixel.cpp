#include "mediaq_mq200_ge_pixel.h"

#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace {

constexpr uint32_t kClipStrideLowBits = 0xFu;
constexpr uint32_t kAlphaShift        = 21u;

}

MediaQMq200Ge::Pixel::Pixel(const uint32_t* r, const Layout& layout, uint8_t* fb, uint32_t fb_size)
    : fb_(fb), fb_size_(fb_size) {
    const uint32_t cmd = r[kGe00Command];
    bpp_    = DepthBytes(r[kGe0ADstStride]);
    mask_   = cerf::ByteWidthMask(bpp_);
    clip_   = (cmd & kCmdEnClip) != 0u;
    stride_ = r[kGe0ADstStride] & layout.stride_mask;
    if (clip_) stride_ &= ~kClipStrideLowBits;
    base_ = r[kGe0BBase] & layout.base_mask;
    rop_  = Rop(cmd);
    cl_ = static_cast<int>(r[kGe05ClipLT] & 0xFFFu);
    ct_ = static_cast<int>((r[kGe05ClipLT] >> 16) & 0xFFFu);
    cr_ = static_cast<int>(r[kGe06ClipRB] & 0xFFFu);
    cb_ = static_cast<int>((r[kGe06ClipRB] >> 16) & 0xFFFu);
    dest_trans_           = (cmd & kCmdColorTrans) != 0u;
    dest_trans_not_equal_ = (cmd & kCmdColorTrPol) != 0u;
    key_          = r[kGe04ColorCmp];
    compare_mask_ = mask_;
    write_mask_   = mask_;
    if (bpp_ == 4u) {
        /* MQ-200 Data Book Table 5-63 bits 22-21: the 32 bpp alpha byte mask. */
        static constexpr uint32_t kAlphaMasks[4] = {0xFFFFFFFFu, 0xFF000000u, 0x00FFFFFFu, 0u};
        compare_mask_ = write_mask_ = kAlphaMasks[(cmd >> kAlphaShift) & 3u];
        if (write_mask_ == 0u) dest_trans_ = false;
    }
}

uint8_t MediaQMq200Ge::Pixel::Rop(uint32_t cmd) {
    const uint32_t rop = cmd & kCmdRopMask;
    if (!(cmd & kCmdRop2)) return static_cast<uint8_t>(rop);
    return static_cast<uint8_t>((rop & 0x0Fu) | ((rop & 0x0Fu) << 4));
}

bool MediaQMq200Ge::Pixel::ReadsPattern(uint8_t rop) {
    return ((rop >> 4) & 0x0Fu) != (rop & 0x0Fu);
}

bool MediaQMq200Ge::Pixel::ReadsSource(uint8_t rop) {
    return ((rop >> 2) & 0x33u) != (rop & 0x33u);
}

bool MediaQMq200Ge::Pixel::Outside(int x, int y) const {
    if (x < 0 || y < 0) return true;
    return clip_ && (x < cl_ || x > cr_ || y < ct_ || y > cb_);
}

bool MediaQMq200Ge::Pixel::Fetch(int64_t addr, uint32_t& value) const {
    if (addr < 0 || static_cast<uint64_t>(addr) + bpp_ > fb_size_) return false;
    value = static_cast<uint32_t>(cerf::le::UN(fb_ + addr, bpp_));
    return true;
}

void MediaQMq200Ge::Pixel::Put(int x, int y, uint32_t p, uint32_t s) {
    if (write_mask_ == 0u) return;
    const uint64_t addr = static_cast<uint64_t>(base_) + static_cast<uint64_t>(y) * stride_ +
                          static_cast<uint64_t>(x) * bpp_;
    if (addr + bpp_ > fb_size_) return;
    uint8_t* const px = fb_ + addr;
    const uint32_t d   = static_cast<uint32_t>(cerf::le::UN(px, bpp_));
    const uint32_t out = Rop3(rop_, p, s, d) & mask_;
    if (dest_trans_ && ((((out ^ key_) & compare_mask_) == 0u) != dest_trans_not_equal_)) return;
    cerf::le::PutN(px, (d & ~write_mask_) | (out & write_mask_), bpp_);
}

}
