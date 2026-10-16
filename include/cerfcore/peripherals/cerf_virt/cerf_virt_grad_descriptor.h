#pragma once

#include "cerf_virt_blt_descriptor.h"

#if defined(__cplusplus)
namespace CerfVirt {
#endif

const uint32_t kCerfGradMagic = 0x43475244u;

const uint32_t kCerfGradAxisH = 0u;
const uint32_t kCerfGradAxisV = 1u;

typedef struct CerfGradDescriptor {
    uint32_t magic;
    uint32_t axis;
    int32_t  start_coord;
    int32_t  end_coord;

    uint16_t start_color[4];
    uint16_t end_color[4];
    CerfBltRect    fill_rect;
    uint32_t band_y_first;
    uint32_t band_y_count;
    CerfBltSurface dst;
} CerfGradDescriptor;

#if defined(__cplusplus)
}
#endif
