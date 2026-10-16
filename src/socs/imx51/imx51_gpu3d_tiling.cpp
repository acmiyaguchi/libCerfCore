#include "imx51_gpu3d_tiling.h"

/* Ford SYNC 2 librenderboy.dll: 0x41CE3440 (2D layout),
   0x41CE3A1C-0x41CE3ABC (CPU tiled upload/readback address). */
uint64_t Imx51Gpu3dTiledAddress(uint32_t base, uint32_t pitch, uint32_t bytes,
    uint32_t x, uint32_t y) {
    const uint64_t macro = (uint64_t(pitch >> 5) * (y >> 5) + (x >> 5)) * (bytes << 7);
    const uint64_t micro = ((x & 7u) + ((y & 6u) << 2)) * bytes;
    const uint64_t offset = macro + ((y >> 3) & 1u) * (bytes << 6) +
        ((micro + ((y & 1u) << 3)) << 1) - (micro & 15u) + (base >> 3);
    const uint32_t bank = (((((y >> 2) & ~1u) + (x >> 3)) << 1) & 6u) + ((y >> 4) & 1u);
    const uint64_t permutation = (bank & ~1u) + ((bank & 1u) << 6);
    return (offset & 63u) + (((offset & 448u) +
        (((offset & ~uint64_t{511}) + (permutation << 2)) << 1)) << 2);
}
