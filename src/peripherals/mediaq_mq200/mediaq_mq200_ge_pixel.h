#pragma once

#include "mediaq_mq200_ge.h"

#include <cstdint>

namespace cerfcore {

class MediaQMq200Ge::Pixel {
public:
    Pixel(const uint32_t* r, const Layout& layout, uint8_t* fb, uint32_t fb_size);

    static uint8_t Rop(uint32_t cmd);
    static bool    ReadsPattern(uint8_t rop);
    static bool    ReadsSource(uint8_t rop);

    uint32_t Bpp() const    { return bpp_; }
    uint32_t Mask() const   { return mask_; }
    uint32_t Stride() const { return stride_; }
    uint32_t Base() const   { return base_; }
    bool ReadsPattern() const { return ReadsPattern(rop_); }
    bool ReadsSource() const  { return ReadsSource(rop_); }

    bool Outside(int x, int y) const;
    bool Fetch(int64_t addr, uint32_t& value) const;
    void Put(int x, int y, uint32_t p, uint32_t s);

private:
    uint8_t* fb_;
    uint32_t fb_size_;
    uint32_t bpp_;
    uint32_t mask_;
    uint32_t stride_;
    uint32_t base_;
    uint8_t  rop_;
    bool     clip_;
    int      cl_, ct_, cr_, cb_;
    bool     dest_trans_;
    bool     dest_trans_not_equal_;
    uint32_t key_;
    uint32_t compare_mask_;
    uint32_t write_mask_;
};

}
