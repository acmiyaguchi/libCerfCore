#pragma once

#include <cerfcore/core/service.h>

#include <cstddef>
#include <cstdint>

namespace cerfcore {

/* The framebuffer + scanout geometry the Rage XL CRTC currently presents, decoded
   from the Mach64 CRTC registers and read by the FrameRenderer. Implemented by the
   AtiRageXl device; resolved via Get<RageXlDisplay>(). */
class RageXlDisplay : public Service {
public:
    using Service::Service;

    struct Frame {
        bool           on      = false;
        uint32_t       width   = 0;    /* visible pixels per line */
        uint32_t       height  = 0;    /* visible lines */
        uint32_t       bpp     = 0;    /* 8 / 15 / 16 / 24 / 32 */
        uint32_t       stride  = 0;    /* bytes per scanline */
        uint32_t       start   = 0;    /* byte offset of the visible base in the framebuffer */
        const uint8_t* fb      = nullptr;
        size_t         fb_size = 0;
    };

    struct Cursor {
        bool           enabled   = false;
        int            x         = 0;   /* CUR_HORZ_VERT_POSN screen top-left */
        int            y         = 0;
        uint32_t       visible_w = 0;   /* 64 - CUR_HORZ_OFF (cropped extent) */
        uint32_t       visible_h = 0;   /* 64 - CUR_VERT_OFF */
        uint32_t       clr0      = 0;   /* 0xRRGGBB for pixel code 00 */
        uint32_t       clr1      = 0;   /* 0xRRGGBB for pixel code 01 */
        const uint8_t* def       = nullptr;  /* FB ptr to the sprite (>= 64*16 bytes) */
    };

    virtual Frame  CurrentFrame()  const = 0;
    virtual Cursor CurrentCursor() const = 0;
};

}
