#pragma once

#include "mediaq_mq200_gc.h"

#include <cstdint>

class MediaQMq200Fp;
class MediaQMq200Power;

class MediaQMq200Panel {
public:
    struct HwCursor {
        bool     shown = false;
        uint32_t x = 0, y = 0, x_off = 0, y_off = 0;
        uint32_t image = 0, fg = 0, bg = 0;
    };

    MediaQMq200Panel(const MediaQMq200Power& power, const MediaQMq200Fp& fp,
                     const MediaQMq200Gc& gc)
        : power_(power), fp_(fp), gc_(gc) {}

    bool     Shown(bool fb_served) const;
    uint32_t Bpp() const;
    uint32_t Width() const;
    uint32_t Height() const;
    uint32_t WindowOffset() const;
    uint32_t Stride() const;
    MediaQMq200Gc::Window Image() const;
    uint32_t Palette(uint32_t index) const;
    HwCursor Cursor(bool fb_served) const;

private:
    const MediaQMq200Power& power_;
    const MediaQMq200Fp&    fp_;
    const MediaQMq200Gc&    gc_;
};
