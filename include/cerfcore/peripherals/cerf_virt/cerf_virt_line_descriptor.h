#pragma once

#include "cerf_virt_blt_descriptor.h"

#if defined(__cplusplus)
namespace CerfVirt {
#endif

const uint32_t kCerfLineMagic = 0x434C494Eu;

typedef struct CerfLineDescriptor {
    uint32_t magic;
    int32_t  x_start;
    int32_t  y_start;
    int32_t  c_pels;
    uint32_t d_m;
    uint32_t d_n;
    int32_t  ll_gamma;
    int32_t  i_dir;
    uint32_t style;
    int32_t  style_state;
    uint32_t solid_color;
    uint32_t mix;
    uint32_t band_y_first;
    uint32_t band_y_count;
    uint32_t has_clip;
    CerfBltRect clip_rect;
    CerfBltSurface dst;
} CerfLineDescriptor;

#if defined(__cplusplus)
}
#endif
