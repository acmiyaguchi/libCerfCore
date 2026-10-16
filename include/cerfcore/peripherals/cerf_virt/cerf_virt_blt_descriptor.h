#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
#else
#include <cstdint>
#endif

#if defined(__cplusplus)
namespace CerfVirt {
#endif

const uint32_t kCerfBltMagic = 0x43424C54u;

const uint32_t kCerfFmt1Bpp  = 0u;
const uint32_t kCerfFmt2Bpp  = 1u;
const uint32_t kCerfFmt4Bpp  = 2u;
const uint32_t kCerfFmt8Bpp  = 3u;
const uint32_t kCerfFmt16Bpp = 4u;
const uint32_t kCerfFmt24Bpp = 5u;
const uint32_t kCerfFmt32Bpp = 6u;

const uint32_t kCerfClearTypeLevelIndices       = 115u;
const uint32_t kCerfClearTypeSampleCountIndices = 180u;

const uint32_t kCerfRotate0   = 0u;
const uint32_t kCerfRotate90  = 1u;
const uint32_t kCerfRotate180 = 2u;
const uint32_t kCerfRotate270 = 3u;

typedef struct CerfBltRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} CerfBltRect;

typedef struct CerfBltSurface {
    uint32_t buffer;
    int32_t  stride;
    uint32_t format;
    uint32_t is_fb_pa;
    uint32_t stage_off;
    uint32_t stage_len;
    uint32_t is_rotate;
    uint32_t rotate;
    uint32_t screen_w;
    uint32_t screen_h;

    uint32_t pal_entries;
    uint32_t mask[4];
} CerfBltSurface;

typedef struct CerfBltDescriptor {
    uint32_t magic;
    uint32_t rop4;
    uint32_t blt_flags;
    uint32_t solid_color;
    uint32_t blend_function;
    uint32_t i_mode;
    uint32_t x_positive;
    uint32_t y_positive;

    uint32_t has_src;
    uint32_t has_mask;
    uint32_t has_brush;
    uint32_t has_clip;
    uint32_t lookup_off;
    uint32_t has_lookup;
    uint32_t convert_active;
    uint32_t to_mono;
    uint32_t mono_bg;

    CerfBltRect dst_rect;
    CerfBltRect src_rect;
    CerfBltRect mask_rect;
    CerfBltRect clip_rect;

    CerfBltSurface dst;
    CerfBltSurface src;
    CerfBltSurface mask;
    CerfBltSurface brush;

    int32_t  brush_ptl_x;
    int32_t  brush_ptl_y;
    uint32_t brush_has_ptl;
    uint32_t brush_width;
    uint32_t brush_height;

    uint32_t band_row_first;
    uint32_t band_row_count;

    uint32_t cleartype_indices;
} CerfBltDescriptor;

#if defined(__cplusplus)
}
#endif
